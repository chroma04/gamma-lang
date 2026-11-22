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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; gm_tcc1rq4n1avb2 f1; } gm_t2aqa0c3djlco;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t2lqu8jsj511d f1; } gm_t5o7duvovobu1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
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
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t2aqa0c3djlco) == 16 && _Alignof(gm_t2aqa0c3djlco) == 8 && offsetof(gm_t2aqa0c3djlco, f0) == 0 && offsetof(gm_t2aqa0c3djlco, f1) == 8, "layout of `(i: usize, x: (file: u32, line: u32))`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_t5hdnnftkup6m f1; } gm_t4c7ikvc472re;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t5o7duvovobu1) == 24 && _Alignof(gm_t5o7duvovobu1) == 8 && offsetof(gm_t5o7duvovobu1, f0) == 0 && offsetof(gm_t5o7duvovobu1, f1) == 8, "layout of `(i: usize, x: (lo: usize, hi: usize))`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint32_t v1; } f1; } gm_tssfa0col9q01;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4c7ikvc472re) == 32 && _Alignof(gm_t4c7ikvc472re) == 8 && offsetof(gm_t4c7ikvc472re, f0) == 0 && offsetof(gm_t4c7ikvc472re, f1) == 8, "layout of `(i: usize, x: Hit)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { void * f0; void * f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_tin97tt4ud0s1 f4; } gm_tqh23fthuf005;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tssfa0col9q01) == 24 && _Alignof(gm_tssfa0col9q01) == 8 && offsetof(gm_tssfa0col9q01, f0) == 0 && offsetof(gm_tssfa0col9q01, f1) == 8, "layout of `ParseError!u32`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqh23fthuf005) == 168 && _Alignof(gm_tqh23fthuf005) == 8 && offsetof(gm_tqh23fthuf005, f0) == 0 && offsetof(gm_tqh23fthuf005, f1) == 8 && offsetof(gm_tqh23fthuf005, f2) == 16 && offsetof(gm_tqh23fthuf005, f3) == 72 && offsetof(gm_tqh23fthuf005, f4) == 128, "layout of `Instrument`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
struct gm_fr_add_counts_hprtdd9 {
    gm_frame h;
    void * l0; /* counts */
    uint64_t l2; /* i */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_block_u0oqu8p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* stmts */
    gm_b0_1 ret;
};
struct gm_fr_bytes_less_r33ee4s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_chars_232hkk6 {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_counted_fns_j3p4pgn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* tests */
    gm_trqfvmjtt4sij l6; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_cover_stmt_bp29hrl {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_coverage_report_hrhbf22 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l5; /* canon */
    gm_trqfvmjtt4sij l6; /* all */
    gm_trqfvmjtt4sij l14; /* lines */
    gm_trqfvmjtt4sij l19; /* files */
    gm_trqfvmjtt4sij l24; /* names */
    gm_trqfvmjtt4sij l43; /* text */
    uint8_t ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_empty_slice_0lkule6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_es387ts {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n0agqu3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n44tb4r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
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
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_v7c9k0s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_g9dfnu7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nqs6or3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_1vrv15m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_d80h595 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tbgr2dj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_vetgq3s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_01jvudh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_78dnvqp {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_dk5gh28 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_70470jk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_vqsve4u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_instrument_mk8clvl {
    gm_frame h;
    gm_tqh23fthuf005 l2; /* cx */
    gm_trqfvmjtt4sij l3; /* counted */
    gm_trqfvmjtt4sij l11; /* stmts */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_test_1bbodje {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_14sicke {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1qnmubu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rdfjble {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pad_o3a3nuu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_path_less_il538gm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l4; /* y */
    uint8_t ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_percent_ie1duk3 {
    gm_frame h;
    double ret;
};
struct gm_fr_point_8frh2ab {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8cklqk5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_lstf2b8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vgae2dr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cn0f3rg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gpvm1qb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t78qm6v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_scope_4n0q0tq {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_sort_by_lh24ei9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_span_slh12dn {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_under_rb6c23o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visit_mnqi05r {
    gm_frame h;
    void * l0; /* cx */
    gm_t710lsrq1m8gk l3; /* l */
    gm_b0_1 ret;
};
struct gm_fr_write_erog63d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_l7mh821 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_lmets0c {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_u0oqu8p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_bytes_less_r33ee4s(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_counted_fns_j3p4pgn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_cover_stmt_bp29hrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_0lkule6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v7c9k0s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ci1c6mk(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_nqs6or3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_01jvudh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_6dvr333(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_78dnvqp(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_introsort_se56bco(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_14sicke(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_pad_o3a3nuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_int_i2lnj6n(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_path_less_il538gm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_point_8frh2ab(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cklqk5(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lstf2b8(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgae2dr(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_a7l7vun(void * a0, gm_tcc1rq4n1avb2 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cn0f3rg(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gpvm1qb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t78qm6v(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e_ssomsr2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_lh24ei9(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq3203i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st80pr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn(const void * a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn_c(gm_trcmmd12cefb1 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_under_rb6c23o(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_mnqi05r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_buk3227(void * a0, gm_t2lqu8jsj511d a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_erog63d(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l7mh821(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lldn1qi(void * a0, gm_tcc1rq4n1avb2 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lmets0c(void * a0, gm_tnpifv20irnph a1);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern void *const gm_hl9hta9cqrqu[];
extern void *const gm_hlnaa5ptob1s[];
extern void *const gm_hlqu6cf845ie[];
extern void *const gm_hlvedue473cr[];
extern uint8_t gm_kp3nl0er6il[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4h0qjq5qavbn[10];
extern const uint8_t gm_s7r1917rmmif7[21];
extern const uint8_t gm_s93p2i934ld6p[5];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sh90fjlgqrlfc[7];
extern const uint8_t gm_skrht660q9e9a[6];
extern const uint8_t gm_sl1f0va0l61bh[23];
extern const uint8_t gm_sl6eotv49v54f[38];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_snge4b9tsq8qc[8];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sofs2uc27fjvk[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const uint8_t gm_ssipp3o8q1jun[11];
extern const uint8_t gm_ssop3uk516o5c[27];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_suc2pvoh9ktdi[6];
extern const gm_tmpl gm_tpl1oodsdaf08oj;
extern const gm_tmpl gm_tplidl0tsmrpak6;
extern const gm_tmpl gm_tplpqtqehhhq7oj;
extern const gm_tmpl gm_tplt392n8hqfhn9;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tybihgc69jv3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_counts_hprtdd9 F;
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = ((uint64_t)0ULL);
    gm_b16_8 t3 = gm_l1;
    gm_l3 = t3;
    {
        gm_node wn4 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl9hta9cqrqu, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l3;
        gm_b0_1 t8 = gm_f_lines_9uns0k2_sqdm19s((void *)NULL, (&wn4), NULL, t7);
        if (gm_unw.kind) {
            goto gm_ll6;
        }
    }
    goto gm_ls9;
gm_ll6:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
    goto gm_lad;
gm_ls9:;
gm_lb1:;
we5:;
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
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_chars_232hkk6 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = ((uint64_t)0ULL);
    gm_b16_8 t3 = gm_l0;
    gm_l2 = t3;
    {
        gm_node wn4 = {.eff = 78927826, .kind = GM_LIT, .clauses = gm_hlnaa5ptob1s, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l2;
        gm_b0_1 t8 = gm_f_runes_5kk1v3e_ssomsr2((void *)NULL, (&wn4), NULL, t7);
        if (gm_unw.kind) {
            goto gm_ll6;
        }
    }
    goto gm_ls9;
gm_ll6:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
    goto gm_lad;
gm_ls9:;
gm_lb1:;
we5:;
    uint64_t t10 = F.l1;
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
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdn4erebgflsv gm_l3;
    {
        {
            {
                void * t4 = gm_l0;
                void * t5 = (void *)(((uint8_t *)t4) + 2184);
                uint32_t t6 = gm_l1;
                gm_f_get_mk00hfd((void *)&gm_l3, t5, t6);
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[64] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 64);
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l2), 64);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 384));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 384) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srclv0dcbbfhves, 589, 12);
    memmove(gm_out, (((uint8_t *)t9) + (uintptr_t)t12 * 64), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_0lkule6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_0lkule6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_es387ts F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n0agqu3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n44tb4r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v7c9k0s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v7c9k0s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_g9dfnu7 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_mnqi05r *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_visit_mnqi05r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_nqs6or3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_nqs6or3 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (!gm_eq0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5)));
    if (t6) {
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
        void * t9 = gm_l1;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t9));
        uint8_t t11 = gm_f_path_less_il538gm((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t11;
        goto gm_fret;
    }
    void * t12 = gm_l0;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 16));
    void * t14 = gm_l1;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 16));
    uint8_t t16 = (t13 < t15);
    F.ret = t16;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1vrv15m F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
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
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l2);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgr7vbgb5cqmg, 185, 34);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 185, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_for_body_d80h595 F;
    uint32_t gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1));
            uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
            if (o6) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 206, 25);
            uint64_t t7 = r5;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1), t7);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 206, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tbgr2dj F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
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
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l4);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgr7vbgb5cqmg, 187, 34);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 187, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_01jvudh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_01jvudh F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tidu2er0d7h95 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skrht660q9e9a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_6dvr333((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt392n8hqfhn9), t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_78dnvqp(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_78dnvqp F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_ci1c6mk(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 16) + 12));
    gm_t9805qtonbsag t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[4] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_dk5gh28 F;
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
    gm_tf5srh4mp4b3i t4 = gm_f_find_hb6lt4h(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_70470jk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n44tb4r();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_vqsve4u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_es387ts();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
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
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
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
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_test_1bbodje F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh90fjlgqrlfc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_l1 = ((uint8_t)1);
    } else {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_snge4b9tsq8qc));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(7));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        gm_l1 = t7;
    }
    uint8_t t8 = gm_l1;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_14sicke(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_14sicke F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_0lkule6();
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
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1qnmubu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n0agqu3();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rdfjble F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_v7c9k0s();
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
inline __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_percent_ie1duk3 F;
    uint64_t gm_l0; /* hit */
    uint64_t gm_l1; /* all */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    double gm_l2;
    uint64_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_l2 = ((double)1e2);
    } else {
        uint64_t t4 = gm_l0;
        double t5 = (double)(t4);
        double t6 = (t5 * ((double)1e2));
        uint64_t t7 = gm_l1;
        double t8 = (double)(t7);
        double t9 = (t6 / t8);
        gm_l2 = t9;
    }
    double t10 = gm_l2;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cklqk5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8cklqk5 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_0lkule6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gpvm1qb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lstf2b8(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_lstf2b8 F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_n0agqu3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t78qm6v(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgae2dr(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vgae2dr F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_v7c9k0s();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_cn0f3rg(t8, ((uint64_t)1ULL), t10);
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
    gm_t2lqu8jsj511d t18 = gm_l1;
    GM_ST(gm_t2lqu8jsj511d, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cn0f3rg(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cn0f3rg F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gpvm1qb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gpvm1qb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t78qm6v(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t78qm6v F;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_lh24ei9(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_lh24ei9 F;
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
    gm_f_introsort_se56bco(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_erog63d(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_erog63d F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tcc1rq4n1avb2 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4h0qjq5qavbn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_lldn1qi(t2, t3, ((void *)&gm_tpl1oodsdaf08oj), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l7mh821(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_l7mh821 F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t2lqu8jsj511d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ssop3uk516o5c));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(26));
    gm_tftb1odc739t5 t5 = gm_f_write_buk3227(t2, t3, ((void *)&gm_tplpqtqehhhq7oj), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lmets0c(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_lmets0c F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ssipp3o8q1jun));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplidl0tsmrpak6), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_u0oqu8p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_u0oqu8p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    gm_tl874t2atnlh3 gm_l6; /* span */
    uint32_t gm_l7; /* id */
    void * gm_l8; /* t */
    uint32_t gm_l9; /* id */
    GM_STACK_CHECK(&F, 572u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_toceffb697p0u gm_l10;
    gm_t9805qtonbsag gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_t9805qtonbsag gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_trqfvmjtt4sij t15;
    gm_f_list_ir0h3tc_2((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t15;
    void * t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)t16));
    gm_l3 = t17;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l4;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t23 = gm_l4;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l5), (((uint8_t *)t21) + (uintptr_t)t23 * 32), 32);
                        }
                        {
                            {
                                gm_toceffb697p0u t24 = gm_f_stmt_span_slh12dn(&(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l5)));
                                gm_l10 = t24;
                                uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l10))[12] != 0);
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l10));
                                gm_l6 = t26;
                                {
                                    {
                                        void * t27 = gm_l0;
                                        gm_tl874t2atnlh3 t28 = gm_l6;
                                        gm_t9805qtonbsag t29 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t27, t28);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l11 = t29;
                                        uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l11))[4] != 0);
                                        if (!t30) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                                        gm_l7 = t31;
                                        void * t32 = (void *)((uint8_t *)&F.l2);
                                        void * t33 = gm_l0;
                                        void * t34 = GM_LD(void *, ((uint8_t *)t33));
                                        uint32_t t35 = gm_l7;
                                        gm_tl874t2atnlh3 t36 = gm_l6;
                                        gm_trcmmd12cefb1 t37;
                                        gm_f_cover_stmt_bp29hrl((void *)&t37, (void *)NULL, cx, NULL, t34, t35, t36);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t38 = gm_f_push_d6c76ud_2(t32, &t37);
                                        gm_l12 = t38;
                                        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcgr7vbgb5cqmg, 137, 39);
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
                        {
                            void * t40 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t41 = gm_f_push_d6c76ud_2(t40, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l5)));
                            gm_l13 = t41;
                        }
                        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcgr7vbgb5cqmg, 139, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 135, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t43 = gm_l4;
            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
            uint64_t t46 = r44;
            gm_l4 = t46;
        }
    }
gm_lb1:;
    {
        {
            void * t47 = gm_l1;
            uint8_t t48 = (uint8_t)(GM_LD(void *, (((uint8_t *)t47) + 16)) != 0);
            if (!t48) {
                goto gm_lb11;
            }
            void * t49 = GM_LD(void *, (((uint8_t *)t47) + 16));
            gm_l8 = t49;
            {
                {
                    void * t50 = gm_l0;
                    void * t51 = gm_l8;
                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 72));
                    gm_t9805qtonbsag t53 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t50, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t53;
                    uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                    if (!t54) {
                        goto gm_lb13;
                    }
                    uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                    gm_l9 = t55;
                    void * t56 = (void *)((uint8_t *)&F.l2);
                    void * t57 = gm_l0;
                    void * t58 = GM_LD(void *, ((uint8_t *)t57));
                    uint32_t t59 = gm_l9;
                    void * t60 = gm_l8;
                    gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
                    gm_trcmmd12cefb1 t62;
                    gm_f_cover_stmt_bp29hrl((void *)&t62, (void *)NULL, cx, NULL, t58, t59, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t63 = gm_f_push_d6c76ud_2(t56, &t62);
                    gm_l15 = t63;
                    uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcgr7vbgb5cqmg, 142, 37);
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
    void * t65 = gm_l1;
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    GM_ST(gm_b16_8, ((uint8_t *)t65), t66);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bytes_less_r33ee4s(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bytes_less_r33ee4s F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t9 = (t7 < t8 ? t7 : t8);
    gm_l2 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = gm_l2;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t13 = gm_l3;
                        gm_l4 = t13;
                        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t16 = gm_l4;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcgr7vbgb5cqmg, 198, 12);
                        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)t16 * 1));
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t20 = gm_l4;
                        if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_srcgr7vbgb5cqmg, 198, 24);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t20 * 1));
                        uint8_t t22 = (!(t17 == t21));
                        if (t22) {
                            void * t23 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t25 = gm_l4;
                            if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_srcgr7vbgb5cqmg, 198, 38);
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t25 * 1));
                            void * t27 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t29 = gm_l4;
                            if ((uintptr_t)t29 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t29, t28, gm_srcgr7vbgb5cqmg, 198, 45);
                            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)t29 * 1));
                            uint8_t t31 = (t26 < t30);
                            F.ret = t31;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 197, 5);
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
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t38 = (t36 < t37);
    F.ret = t38;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_counted_fns_j3p4pgn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_counted_fns_j3p4pgn F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* b */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* sig */
    gm_b16_8 gm_l11; /* name */
    uint8_t gm_l12; /* generated */
    uint8_t gm_l13; /* in_test */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tl874t2atnlh3 gm_l16; /* t */
    uint8_t gm_l17; /* has_body */
    GM_STACK_CHECK(&F, 959u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tdgfduqjheqdm gm_l18;
    void * gm_l19;
    gm_tftb1odc739t5 gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    gm_tdgfduqjheqdm gm_l23;
    void * gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    {
        gm_trqfvmjtt4sij t22;
        gm_f_list_7s4om10((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t22;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 344) + 8));
    gm_l2 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l3;
            uint64_t t26 = gm_l2;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l3;
                        gm_l4 = t28;
                        void * t29 = gm_l0;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 344));
                        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 344) + 8));
                        uint64_t t32 = gm_l4;
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_srcgr7vbgb5cqmg, 84, 24);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t30) + (uintptr_t)t32 * 136));
                        uint8_t t34 = gm_f_is_test_1bbodje(t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t35 = (uint8_t)!t34;
                        if (t35) {
                            goto gm_lb3;
                        }
                        void * t36 = gm_l0;
                        uint64_t t37 = gm_l4;
                        uint32_t t38 = (uint32_t)t37;
                        gm_f_def_of_m512sbu((void *)&gm_l18, t36, t38);
                        void * t39 = GM_LD(void *, (((uint8_t *)&gm_l18) + 40));
                        gm_l19 = t39;
                        uint8_t t40 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l19)) != 0);
                        if (t40) {
                            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l19));
                            gm_l5 = t41;
                        } else {
                            goto gm_lb3;
                        }
                        void * t42 = (void *)((uint8_t *)&F.l1);
                        void * t43 = gm_l5;
                        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
                        gm_tftb1odc739t5 t45 = gm_f_push_0mk7udh(t42, GM_CONV(gm_cseahvg8v7p0vs, t44));
                        gm_l20 = t45;
                        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcgr7vbgb5cqmg, 86, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 83, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l3;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l3 = t50;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t51;
        gm_f_list_s83jn2q((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t51;
    }
    gm_l8 = ((uint64_t)0ULL);
    void * t52 = gm_l0;
    uint64_t t53 = GM_LD(uint64_t, ((((uint8_t *)t52) + 344) + 8));
    gm_l7 = t53;
    {
        for (;;) {
            uint64_t t54 = gm_l8;
            uint64_t t55 = gm_l7;
            uint8_t t56 = (t54 < t55);
            if (!t56) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t57 = gm_l8;
                        gm_l9 = t57;
                        void * t58 = gm_l0;
                        void * t59 = GM_LD(void *, (((uint8_t *)t58) + 344));
                        uint64_t t60 = GM_LD(uint64_t, ((((uint8_t *)t58) + 344) + 8));
                        uint64_t t61 = gm_l9;
                        if ((uintptr_t)t61 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t61, t60, gm_srcgr7vbgb5cqmg, 90, 17);
                        void * t62 = (void *)(((uint8_t *)t59) + (uintptr_t)t61 * 136);
                        gm_l10 = t62;
                        void * t63 = gm_l10;
                        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)t63));
                        gm_l11 = t64;
                        gm_b16_8 t65 = gm_l11;
                        gm_b16_8 t66;
                        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s1f440st4mnkp));
                        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
                        uint8_t t67 = gm_f_starts_with_6vvt01e(t65, t66);
                        if (t67) {
                            gm_b16_8 t68 = gm_l11;
                            gm_b16_8 t69;
                            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_s93p2i934ld6p));
                            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(4));
                            uint8_t t70 = (!gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                            gm_l22 = t70;
                        } else {
                            gm_l22 = ((uint8_t)0);
                        }
                        uint8_t t71 = gm_l22;
                        if (t71) {
                            gm_b16_8 t72 = gm_l11;
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_ss7qhfi2s00na));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(10));
                            uint8_t t74 = (!gm_eq0iqiubfm7e(((uint8_t *)&t72), ((uint8_t *)&t73)));
                            gm_l21 = t74;
                        } else {
                            gm_l21 = ((uint8_t)0);
                        }
                        uint8_t t75 = gm_l21;
                        gm_l12 = t75;
                        gm_l13 = ((uint8_t)0);
                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
                        gm_l14 = t76;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t77 = gm_l15;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint8_t t79 = (t77 < t78);
                                if (!t79) {
                                    goto gm_lb11;
                                }
                                {
                                    void * t80 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint64_t t82 = gm_l15;
                                    {
                                        {
                                            gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + (uintptr_t)t82 * 12));
                                            gm_l16 = t83;
                                            {
                                                {
                                                    uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)&gm_l16));
                                                    void * t85 = gm_l10;
                                                    uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)t85) + 16));
                                                    uint8_t t87 = ((t84 == t86));
                                                    if (!t87) {
                                                        goto gm_lb17;
                                                    }
                                                    uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
                                                    void * t89 = gm_l10;
                                                    uint32_t t90 = GM_LD(uint32_t, ((((uint8_t *)t89) + 16) + 4));
                                                    uint8_t t91 = (t88 <= t90);
                                                    if (!t91) {
                                                        goto gm_lb17;
                                                    }
                                                    void * t92 = gm_l10;
                                                    uint32_t t93 = GM_LD(uint32_t, ((((uint8_t *)t92) + 16) + 8));
                                                    uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 8));
                                                    uint8_t t95 = (t93 <= t94);
                                                    if (!t95) {
                                                        goto gm_lb17;
                                                    }
                                                    gm_l13 = ((uint8_t)1);
                                                    goto gm_lb16;
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 94, 9);
                                    }
gm_lb14:;
                                }
gm_lb13:;
                                uint64_t t96 = gm_l15;
                                uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                                uint64_t t99 = r97;
                                gm_l15 = t99;
                            }
                        }
gm_lb11:;
                        void * t100 = gm_l0;
                        uint64_t t101 = gm_l9;
                        uint32_t t102 = (uint32_t)t101;
                        gm_f_def_of_m512sbu((void *)&gm_l23, t100, t102);
                        void * t103 = GM_LD(void *, (((uint8_t *)&gm_l23) + 40));
                        gm_l24 = t103;
                        {
                            {
                                uint8_t t104 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l24)) != 0);
                                if (t104) {
                                    goto gm_lb19;
                                }
                                gm_l17 = ((uint8_t)0);
                                goto gm_lb18;
                            }
gm_lb19:;
                            {
                                gm_l17 = ((uint8_t)1);
                                goto gm_lb18;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 97, 21);
                        }
gm_lb18:;
                        void * t105 = (void *)((uint8_t *)&F.l6);
                        uint8_t t106 = gm_l17;
                        if (t106) {
                            uint8_t t107 = gm_l12;
                            uint8_t t108 = (uint8_t)!t107;
                            gm_l27 = t108;
                        } else {
                            gm_l27 = ((uint8_t)0);
                        }
                        uint8_t t109 = gm_l27;
                        if (t109) {
                            gm_b16_8 t110 = gm_l11;
                            uint8_t t111 = gm_f_is_test_1bbodje(t110);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t112 = (uint8_t)!t111;
                            gm_l26 = t112;
                        } else {
                            gm_l26 = ((uint8_t)0);
                        }
                        uint8_t t113 = gm_l26;
                        if (t113) {
                            uint8_t t114 = gm_l13;
                            uint8_t t115 = (uint8_t)!t114;
                            gm_l25 = t115;
                        } else {
                            gm_l25 = ((uint8_t)0);
                        }
                        uint8_t t116 = gm_l25;
                        gm_tftb1odc739t5 t117 = gm_f_push_vd1290p(t105, t116);
                        gm_l28 = t117;
                        uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcgr7vbgb5cqmg, 98, 9);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 89, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t119 = gm_l8;
            uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
            uint64_t t122 = r120;
            gm_l8 = t122;
        }
    }
gm_lb6:;
    {
        memmove(gm_out, ((uint8_t *)&F.l6), 40);
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
inline __attribute__((aligned(4))) void gm_f_cover_stmt_bp29hrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_cover_stmt_bp29hrl F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t5 = gm_l1;
    gm_t8588ma2cr9d7 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)82ULL));
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t5);
    gm_b16_8 t7 = gm_f_empty_slice_g3tddrb_2();
    gm_t9q8lhljes74c t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 12, 0);
    GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 16), t7);
    gm_tl874t2atnlh3 t9 = gm_l2;
    gm_to0f5qn75cgg8 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)33ULL));
    GM_ST(gm_t9q8lhljes74c, ((uint8_t *)&t10 + 8), t8);
    void * t11 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t10, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 8), t11);
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
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_coverage_report_hrhbf22 F;
    void * gm_l0; /* s */
    void * gm_l1; /* scope */
    void * gm_l2; /* map */
    gm_b16_8 gm_l3; /* counts */
    gm_th2lqbo77pvsc gm_l4; /* lcov */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_tcc1rq4n1avb2 gm_l10; /* p */
    gm_b16_8 gm_l11; /* given */
    gm_b16_8 gm_l12; /* c */
    gm_b16_8 gm_l13; /* path */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t5hdnnftkup6m gm_l17; /* h */
    uint64_t gm_l18; /* n */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    gm_t5hdnnftkup6m gm_l23; /* h */
    uint64_t gm_l25; /* width */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t2lqu8jsj511d gm_l28; /* f */
    gm_b16_8 gm_l29; /* path */
    gm_b16_8 gm_l30; /* rel */
    uint64_t gm_l31; /* total_hit */
    uint64_t gm_l32; /* total_all */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    gm_t2lqu8jsj511d gm_l36; /* f */
    uint64_t gm_l37; /* hit */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t5hdnnftkup6m gm_l40; /* h */
    uint64_t gm_l41; /* n */
    gm_b16_8 gm_l42; /* out */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_t2lqu8jsj511d gm_l46; /* f */
    uint64_t gm_l47; /* hit */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    gm_t5hdnnftkup6m gm_l50; /* h */
    gm_toe5fmr3vvf4d gm_l51; /* e */
    GM_STACK_CHECK(&F, 3814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t2aqa0c3djlco gm_l52;
    gm_th2lqbo77pvsc gm_l53;
    gm_th2lqbo77pvsc gm_l54;
    gm_tftb1odc739t5 gm_l55;
    uint32_t gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_t4c7ikvc472re gm_l59;
    uint8_t gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_b16_8 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_t5o7duvovobu1 gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_ttln0d0hd70en gm_l70;
    {
        gm_t4c7ipjj9s3fp t46;
        gm_f_hash_map_tl7apid((void *)&t46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t46;
        gm_trqfvmjtt4sij t47;
        gm_f_list_14sicke((void *)&t47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t47;
        void * t48 = gm_l2;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)t48));
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_l7 = t50;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t51 = gm_l8;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t53 = (t51 < t52);
                if (!t53) {
                    goto gm_lb1;
                }
                {
                    uint64_t t54 = gm_l8;
                    void * t55 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t57 = gm_l8;
                    gm_tcc1rq4n1avb2 t58 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t55) + (uintptr_t)t57 * 8));
                    gm_t2aqa0c3djlco t59;
                    GM_ST(uint64_t, ((uint8_t *)&t59 + 0), t54);
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t59 + 8), t58);
                    gm_l52 = t59;
                    {
                        {
                            uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l52));
                            gm_l9 = t60;
                            gm_tcc1rq4n1avb2 t61 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&gm_l52) + 8));
                            gm_l10 = t61;
                            {
                                void * t62 = (void *)((uint8_t *)&F.l5);
                                uint32_t t63 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                gm_f_get_gnmase9((void *)&gm_l53, t62, t63);
                            }
                            uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l53))[16] != 0);
                            if (t64) {
                                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l53));
                                gm_l13 = t65;
                            } else {
                                void * t66 = gm_l0;
                                void * t67 = GM_LD(void *, ((uint8_t *)t66));
                                void * t68 = GM_LD(void *, ((uint8_t *)t67));
                                uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                void * t70 = gm_f_file_o15fqku(t68, t69);
                                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)t70));
                                gm_l11 = t71;
                                gm_b16_8 t72 = gm_l11;
                                gm_f_canonicalize_epo7cln((void *)&gm_l54, (void *)NULL, cx, NULL, t72);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l54))[16] != 0);
                                if (t73) {
                                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l54));
                                    gm_l12 = t74;
                                } else {
                                    gm_b16_8 t75 = gm_l11;
                                    gm_l12 = t75;
                                }
                                void * t76 = (void *)((uint8_t *)&F.l5);
                                uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                gm_b16_8 t78 = gm_l12;
                                gm_tftb1odc739t5 t79 = gm_f_put_po09vud(t76, t77, t78);
                                gm_l55 = t79;
                                uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcgr7vbgb5cqmg, 229, 13);
                                gm_b16_8 t81 = gm_l12;
                                gm_l13 = t81;
                            }
                            gm_b16_8 t82 = gm_l13;
                            gm_l13 = t82;
                            {
                                void * t83 = (void *)((uint8_t *)&F.l6);
                                gm_b16_8 t84 = gm_l13;
                                uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                                uint64_t t86 = gm_l9;
                                uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint8_t t88 = (t86 < t87);
                                if (t88) {
                                    void * t89 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t91 = gm_l9;
                                    uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)t89) + (uintptr_t)t91 * 4));
                                    gm_l56 = t92;
                                } else {
                                    gm_l56 = ((uint32_t)0ULL);
                                }
                                uint32_t t93 = gm_l56;
                                gm_t5hdnnftkup6m t94;
                                GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t84);
                                GM_ST(uint32_t, ((uint8_t *)&t94 + 16), t85);
                                GM_ST(uint32_t, ((uint8_t *)&t94 + 20), t93);
                                gm_tftb1odc739t5 t95 = gm_f_push_8cklqk5(t83, &t94);
                                gm_l57 = t95;
                            }
                            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                            if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcgr7vbgb5cqmg, 232, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 225, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t97 = gm_l8;
                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                uint64_t t100 = r98;
                gm_l8 = t100;
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_closure t102 = ((gm_closure){((void *)gm_f_fn_nqs6or3), NULL, cx});
        gm_f_sort_by_lh24ei9(t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t103;
        gm_f_list_14sicke((void *)&t103, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t103;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l15 = t104;
        gm_l16 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t105 = gm_l16;
                uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t107 = (t105 < t106);
                if (!t107) {
                    goto gm_lb6;
                }
                {
                    void * t108 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t110 = gm_l16;
                    {
                        {
                            memmove(((uint8_t *)&gm_l17), (((uint8_t *)t108) + (uintptr_t)t110 * 24), 24);
                            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            gm_l18 = t111;
                            {
                                {
                                    uint64_t t112 = gm_l18;
                                    uint8_t t113 = (t112 > ((uint64_t)0ULL));
                                    if (!t113) {
                                        goto gm_lb12;
                                    }
                                    void * t114 = GM_LD(void *, ((uint8_t *)&F.l14));
                                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                    uint64_t t116 = gm_l18;
                                    uint64_t r117; bool o118 = __builtin_sub_overflow(t116, ((uint64_t)1ULL), &r117);
                                    if (o118) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 242, 34);
                                    uint64_t t119 = r117;
                                    if ((uintptr_t)t119 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t119, t115, gm_srcgr7vbgb5cqmg, 242, 22);
                                    gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)t114) + (uintptr_t)t119 * 24));
                                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                    uint8_t t122 = (gm_eq0iqiubfm7e(((uint8_t *)&t120), ((uint8_t *)&t121)));
                                    if (!t122) {
                                        goto gm_lb12;
                                    }
                                    void * t123 = GM_LD(void *, ((uint8_t *)&F.l14));
                                    uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                    uint64_t t125 = gm_l18;
                                    uint64_t r126; bool o127 = __builtin_sub_overflow(t125, ((uint64_t)1ULL), &r126);
                                    if (o127) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 242, 72);
                                    uint64_t t128 = r126;
                                    if ((uintptr_t)t128 >= (uintptr_t)(t124)) gm_trap_index((uintptr_t)t128, t124, gm_srcgr7vbgb5cqmg, 242, 60);
                                    uint32_t t129 = GM_LD(uint32_t, ((((uint8_t *)t123) + (uintptr_t)t128 * 24) + 16));
                                    uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)&gm_l17) + 16));
                                    uint8_t t131 = ((t129 == t130));
                                    if (!t131) {
                                        goto gm_lb12;
                                    }
                                    void * t132 = GM_LD(void *, ((uint8_t *)&F.l14));
                                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                    uint64_t t134 = gm_l18;
                                    uint64_t r135; bool o136 = __builtin_sub_overflow(t134, ((uint64_t)1ULL), &r135);
                                    if (o136) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 243, 25);
                                    uint64_t t137 = r135;
                                    if ((uintptr_t)t137 >= (uintptr_t)(t133)) gm_trap_index((uintptr_t)t137, t133, gm_srcgr7vbgb5cqmg, 243, 13);
                                    void * t138 = GM_LD(void *, ((uint8_t *)&F.l14));
                                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                    uint64_t t140 = gm_l18;
                                    uint64_t r141; bool o142 = __builtin_sub_overflow(t140, ((uint64_t)1ULL), &r141);
                                    if (o142) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 243, 51);
                                    uint64_t t143 = r141;
                                    if ((uintptr_t)t143 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t143, t139, gm_srcgr7vbgb5cqmg, 243, 39);
                                    uint32_t t144 = GM_LD(uint32_t, ((((uint8_t *)t138) + (uintptr_t)t143 * 24) + 20));
                                    uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&gm_l17) + 20));
                                    uint32_t t146 = (t144 > t145 ? t144 : t145);
                                    GM_ST(uint32_t, ((((uint8_t *)t132) + (uintptr_t)t137 * 24) + 20), t146);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                void * t147 = (void *)((uint8_t *)&F.l14);
                                gm_tftb1odc739t5 t148 = gm_f_push_8cklqk5(t147, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l17)));
                                gm_l58 = t148;
                                uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcgr7vbgb5cqmg, 245, 13);
                            }
gm_lb11:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 240, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t150 = gm_l16;
                uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                uint64_t t153 = r151;
                gm_l16 = t153;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t154;
        gm_f_list_rdfjble((void *)&t154, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l19 = t154;
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
        gm_l20 = t156;
        gm_l21 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t157 = gm_l21;
                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint8_t t159 = (t157 < t158);
                if (!t159) {
                    goto gm_lb13;
                }
                {
                    {
                        uint64_t t160 = gm_l21;
                        void * t161 = GM_LD(void *, ((uint8_t *)&gm_l20));
                        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint64_t t163 = gm_l21;
                        gm_t5hdnnftkup6m t164 = GM_LD(gm_t5hdnnftkup6m, (((uint8_t *)t161) + (uintptr_t)t163 * 24));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l59 + 0), t160);
                        GM_ST(gm_t5hdnnftkup6m, ((uint8_t *)&gm_l59 + 8), t164);
                    }
                    {
                        {
                            uint64_t t165 = GM_LD(uint64_t, ((uint8_t *)&gm_l59));
                            gm_l22 = t165;
                            memmove(((uint8_t *)&gm_l23), (((uint8_t *)&gm_l59) + 8), 24);
                            uint64_t t166 = gm_l22;
                            uint8_t t167 = ((t166 == ((uint64_t)0ULL)));
                            if (t167) {
                                gm_l60 = ((uint8_t)1);
                            } else {
                                void * t168 = GM_LD(void *, ((uint8_t *)&F.l14));
                                uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                uint64_t t170 = gm_l22;
                                uint64_t r171; bool o172 = __builtin_sub_overflow(t170, ((uint64_t)1ULL), &r171);
                                if (o172) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 251, 34);
                                uint64_t t173 = r171;
                                if ((uintptr_t)t173 >= (uintptr_t)(t169)) gm_trap_index((uintptr_t)t173, t169, gm_srcgr7vbgb5cqmg, 251, 22);
                                gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)t168) + (uintptr_t)t173 * 24));
                                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                uint8_t t176 = (!gm_eq0iqiubfm7e(((uint8_t *)&t174), ((uint8_t *)&t175)));
                                gm_l60 = t176;
                            }
                            uint8_t t177 = gm_l60;
                            if (t177) {
                                void * t178 = (void *)((uint8_t *)&F.l19);
                                uint64_t t179 = gm_l22;
                                uint64_t t180 = gm_l22;
                                uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                                if (o182) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 251, 85);
                                uint64_t t183 = r181;
                                gm_t2lqu8jsj511d t184;
                                GM_ST(uint64_t, ((uint8_t *)&t184 + 0), t179);
                                GM_ST(uint64_t, ((uint8_t *)&t184 + 8), t183);
                                gm_tftb1odc739t5 t185 = gm_f_push_vgae2dr(t178, t184);
                                gm_l61 = t185;
                                uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcgr7vbgb5cqmg, 251, 62);
                            } else {
                                void * t187 = GM_LD(void *, ((uint8_t *)&F.l19));
                                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                uint64_t r190; bool o191 = __builtin_sub_overflow(t189, ((uint64_t)1ULL), &r190);
                                if (o191) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 251, 115);
                                uint64_t t192 = r190;
                                if ((uintptr_t)t192 >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)t192, t188, gm_srcgr7vbgb5cqmg, 251, 103);
                                uint64_t t193 = gm_l22;
                                uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                                if (o195) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 251, 141);
                                uint64_t t196 = r194;
                                GM_ST(uint64_t, ((((uint8_t *)t187) + (uintptr_t)t192 * 16) + 8), t196);
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 250, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t197 = gm_l21;
                uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                uint64_t t200 = r198;
                gm_l21 = t200;
            }
        }
gm_lb13:;
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srcgr7vbgb5cqmg, 253, 5)) {
            goto gm_lad;
        }
    }
    {
        uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
        uint8_t t202 = ((t201 == ((uint64_t)0ULL)));
        if (t202) {
            if (gm_pf1m4b8q4e78(cx, gm_sl6eotv49v54f, 37, gm_srcgr7vbgb5cqmg, 255, 9)) {
                goto gm_lad;
            }
        } else {
            {
                gm_trqfvmjtt4sij t203;
                gm_f_list_epvducg((void *)&t203, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l24 = t203;
            }
            gm_b16_8 t204;
            GM_ST(void *, (uint8_t *)&t204, (void *)(gm_suc2pvoh9ktdi));
            GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(5));
            gm_l62 = t204;
            uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
            gm_l25 = t205;
            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
            gm_l26 = t206;
            gm_l27 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t207 = gm_l27;
                    uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint8_t t209 = (t207 < t208);
                    if (!t209) {
                        goto gm_lb18;
                    }
                    {
                        void * t210 = GM_LD(void *, ((uint8_t *)&gm_l26));
                        uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                        uint64_t t212 = gm_l27;
                        {
                            {
                                gm_t2lqu8jsj511d t213 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t210) + (uintptr_t)t212 * 16));
                                gm_l28 = t213;
                                void * t214 = GM_LD(void *, ((uint8_t *)&F.l14));
                                uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                uint64_t t216 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                                if ((uintptr_t)t216 >= (uintptr_t)(t215)) gm_trap_index((uintptr_t)t216, t215, gm_srcgr7vbgb5cqmg, 260, 21);
                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)t214) + (uintptr_t)t216 * 24));
                                gm_l29 = t217;
                                gm_b16_8 t218 = gm_l29;
                                gm_l30 = t218;
                                {
                                    {
                                        gm_b16_8 t219 = gm_l29;
                                        void * t220 = gm_l1;
                                        gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)t220));
                                        uint8_t t222 = gm_f_under_rb6c23o(t219, t221);
                                        if (!t222) {
                                            goto gm_lb24;
                                        }
                                        uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                        void * t224 = gm_l1;
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)t224) + 8));
                                        uint8_t t226 = (t223 > t225);
                                        if (!t226) {
                                            goto gm_lb24;
                                        }
                                        void * t227 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                        uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                        void * t229 = gm_l1;
                                        uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)t229) + 8));
                                        uint64_t t231 = (uintptr_t)t230;
                                        uint64_t t232 = (uintptr_t)t228;
                                        if (t231 > t232 || t232 > (uintptr_t)t228) gm_trap_slice(t231, t232, t228, gm_srcgr7vbgb5cqmg, 263, 23);
                                        uint64_t t233 = ((uintptr_t)t228) - (uintptr_t)t230;
                                        gm_b16_8 t234;
                                        GM_ST(void *, (uint8_t *)&t234, (void *)(((uint8_t *)t227 + (uintptr_t)t230 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(t233));
                                        gm_l30 = t234;
                                        gm_b16_8 t235 = gm_l30;
                                        gm_b16_8 t236;
                                        GM_ST(void *, (uint8_t *)&t236, (void *)(gm_st3ja0nqi37bv));
                                        GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(1));
                                        uint8_t t237 = gm_f_starts_with_6vvt01e(t235, t236);
                                        if (t237) {
                                            void * t238 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                            uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                            uint64_t t240 = (uintptr_t)((uint64_t)1ULL);
                                            uint64_t t241 = (uintptr_t)t239;
                                            if (t240 > t241 || t241 > (uintptr_t)t239) gm_trap_slice(t240, t241, t239, gm_srcgr7vbgb5cqmg, 264, 49);
                                            uint64_t t242 = ((uintptr_t)t239) - (uintptr_t)((uint64_t)1ULL);
                                            gm_b16_8 t243;
                                            GM_ST(void *, (uint8_t *)&t243, (void *)(((uint8_t *)t238 + (uintptr_t)((uint64_t)1ULL) * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(t242));
                                            gm_l30 = t243;
                                        }
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                }
gm_lb23:;
                                void * t244 = (void *)((uint8_t *)&F.l24);
                                gm_b16_8 t245 = gm_l30;
                                gm_tftb1odc739t5 t246 = gm_f_push_3ef746b(t244, t245);
                                gm_l63 = t246;
                                uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srcgr7vbgb5cqmg, 266, 13);
                                uint64_t t248 = gm_l25;
                                gm_b16_8 t249 = gm_l30;
                                uint64_t t250 = gm_f_chars_232hkk6(t249);
                                uint64_t t251 = (t248 > t250 ? t248 : t250);
                                gm_l25 = t251;
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 259, 9);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t252 = gm_l27;
                    uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                    uint64_t t255 = r253;
                    gm_l27 = t255;
                }
            }
gm_lb18:;
            if (gm_pf1m4b8q4e78(cx, gm_sl1f0va0l61bh, 22, gm_srcgr7vbgb5cqmg, 269, 9)) {
                goto gm_lad;
            }
            gm_l31 = ((uint64_t)0ULL);
            gm_l32 = ((uint64_t)0ULL);
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
            gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
            gm_l33 = t257;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t258 = gm_l34;
                    uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t260 = (t258 < t259);
                    if (!t260) {
                        goto gm_lb25;
                    }
                    {
                        uint64_t t261 = gm_l34;
                        void * t262 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t264 = gm_l34;
                        gm_t2lqu8jsj511d t265 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t262) + (uintptr_t)t264 * 16));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l64 + 0), t261);
                        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l64 + 8), t265);
                        {
                            {
                                uint64_t t266 = GM_LD(uint64_t, ((uint8_t *)&gm_l64));
                                gm_l35 = t266;
                                gm_t2lqu8jsj511d t267 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&gm_l64) + 8));
                                gm_l36 = t267;
                                gm_l37 = ((uint64_t)0ULL);
                                void * t268 = GM_LD(void *, ((uint8_t *)&F.l14));
                                uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                uint64_t t270 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                uint64_t t272 = (uintptr_t)t270;
                                uint64_t t273 = (uintptr_t)t271;
                                if (t272 > t273 || t273 > (uintptr_t)t269) gm_trap_slice(t272, t273, t269, gm_srcgr7vbgb5cqmg, 274, 22);
                                uint64_t t274 = ((uintptr_t)t271) - (uintptr_t)t270;
                                gm_b16_8 t275;
                                GM_ST(void *, (uint8_t *)&t275, (void *)(((uint8_t *)t268 + (uintptr_t)t270 * 24)));
                                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(t274));
                                gm_l38 = t275;
                                gm_l39 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t276 = gm_l39;
                                        uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                        uint8_t t278 = (t276 < t277);
                                        if (!t278) {
                                            goto gm_lb30;
                                        }
                                        {
                                            void * t279 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                            uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint64_t t281 = gm_l39;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l40), (((uint8_t *)t279) + (uintptr_t)t281 * 24), 24);
                                                    uint32_t t282 = GM_LD(uint32_t, (((uint8_t *)&gm_l40) + 20));
                                                    uint8_t t283 = (t282 > ((uint32_t)0ULL));
                                                    if (t283) {
                                                        uint64_t t284 = gm_l37;
                                                        uint64_t r285; bool o286 = __builtin_add_overflow(t284, ((uint64_t)1ULL), &r285);
                                                        if (o286) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 275, 33);
                                                        uint64_t t287 = r285;
                                                        gm_l37 = t287;
                                                    }
                                                    goto gm_lb33;
                                                }
gm_lb34:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 274, 13);
                                            }
gm_lb33:;
                                        }
gm_lb32:;
                                        uint64_t t288 = gm_l39;
                                        uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                                        uint64_t t291 = r289;
                                        gm_l39 = t291;
                                    }
                                }
gm_lb30:;
                                uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                uint64_t t293 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                uint64_t r294; bool o295 = __builtin_sub_overflow(t292, t293, &r294);
                                if (o295) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 277, 18);
                                uint64_t t296 = r294;
                                gm_l41 = t296;
                                uint64_t t297 = gm_l31;
                                uint64_t t298 = gm_l37;
                                uint64_t r299; bool o300 = __builtin_add_overflow(t297, t298, &r299);
                                if (o300) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 278, 13);
                                uint64_t t301 = r299;
                                gm_l31 = t301;
                                uint64_t t302 = gm_l32;
                                uint64_t t303 = gm_l41;
                                uint64_t r304; bool o305 = __builtin_add_overflow(t302, t303, &r304);
                                if (o305) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 279, 13);
                                uint64_t t306 = r304;
                                gm_l32 = t306;
                                void * t307 = GM_LD(void *, ((uint8_t *)&F.l24));
                                uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&F.l24) + 8));
                                uint64_t t309 = gm_l35;
                                if ((uintptr_t)t309 >= (uintptr_t)(t308)) gm_trap_index((uintptr_t)t309, t308, gm_srcgr7vbgb5cqmg, 280, 52);
                                gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)t307) + (uintptr_t)t309 * 16));
                                uint64_t t311 = gm_l25;
                                gm_b16_8 t312 = gm_f_pad_o3a3nuu((void *)NULL, cx, NULL, t310, t311);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t313 = gm_l37;
                                uint64_t t314 = gm_l41;
                                uint64_t t315 = gm_l37;
                                uint64_t t316 = gm_l41;
                                double t317 = gm_f_percent_ie1duk3(t315, t316);
                                gm_tidu2er0d7h95 t318;
                                GM_ST(double, ((uint8_t *)&t318 + 0), t317);
                                gm_f_format_01jvudh((void *)&gm_l65, (void *)NULL, cx, NULL, t318);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srcgr7vbgb5cqmg, 280, 84);
                                gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                if (gm_pf1m4b8q4e78(cx, gm_sgankvlme129s, 2, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                    goto gm_lad;
                                }
                                if ((GM_LD(uintptr_t, ((uint8_t *)&t312) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t312)), GM_LD(uintptr_t, ((uint8_t *)&t312) + 8), gm_srcgr7vbgb5cqmg, 280, 13))) {
                                    goto gm_lad;
                                }
                                if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                    goto gm_lad;
                                }
                                gm_buf b321 = {0};
                                gm_format_spec(&b321, &gm_ty6groq37ram, ((uint8_t *)&t313), &(gm_spec){32, 2, 0, 0, 0, 0, 5u, -1});
                                if (b321.len != 0) {
                                    gm_b16_8 t322;
                                    GM_ST(void *, (uint8_t *)&t322, (void *)(b321.data));
                                    GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(b321.len));
                                    gm_pfmaojsuds6a(cx, t322, gm_srcgr7vbgb5cqmg, 280, 13);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                gm_buf_free(&b321);
                                if (gm_pf1m4b8q4e78(cx, gm_st3ja0nqi37bv, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                    goto gm_lad;
                                }
                                gm_buf b323 = {0};
                                gm_format_spec(&b323, &gm_ty6groq37ram, ((uint8_t *)&t314), &(gm_spec){32, 1, 0, 0, 0, 0, 5u, -1});
                                if (b323.len != 0) {
                                    gm_b16_8 t324;
                                    GM_ST(void *, (uint8_t *)&t324, (void *)(b323.data));
                                    GM_ST(uintptr_t, (uint8_t *)&t324 + 8, (uintptr_t)(b323.len));
                                    gm_pfmaojsuds6a(cx, t324, gm_srcgr7vbgb5cqmg, 280, 13);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                gm_buf_free(&b323);
                                if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                    goto gm_lad;
                                }
                                gm_buf b325 = {0};
                                gm_format_spec(&b325, &gm_tybihgc69jv3, ((uint8_t *)&t320), &(gm_spec){32, 2, 0, 0, 0, 0, 6u, -1});
                                if (b325.len != 0) {
                                    gm_b16_8 t326;
                                    GM_ST(void *, (uint8_t *)&t326, (void *)(b325.data));
                                    GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(b325.len));
                                    gm_pfmaojsuds6a(cx, t326, gm_srcgr7vbgb5cqmg, 280, 13);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                gm_buf_free(&b325);
                                if (gm_pf1m4b8q4e78(cx, gm_sofs2uc27fjvk, 2, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                    goto gm_lad;
                                }
                                goto gm_lb28;
                            }
gm_lb29:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 272, 9);
                        }
gm_lb28:;
                    }
gm_lb27:;
                    uint64_t t327 = gm_l34;
                    uint64_t r328; bool o329 = __builtin_add_overflow(t327, ((uint64_t)1ULL), &r328);
                    uint64_t t330 = r328;
                    gm_l34 = t330;
                }
            }
gm_lb25:;
            {
                gm_b16_8 t331;
                GM_ST(void *, (uint8_t *)&t331, (void *)(gm_suc2pvoh9ktdi));
                GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(5));
                uint64_t t332 = gm_l25;
                gm_b16_8 t333 = gm_f_pad_o3a3nuu((void *)NULL, cx, NULL, t331, t332);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t334 = gm_l31;
                uint64_t t335 = gm_l32;
                uint64_t t336 = gm_l31;
                uint64_t t337 = gm_l32;
                double t338 = gm_f_percent_ie1duk3(t336, t337);
                gm_tidu2er0d7h95 t339;
                GM_ST(double, ((uint8_t *)&t339 + 0), t338);
                gm_f_format_01jvudh((void *)&gm_l66, (void *)NULL, cx, NULL, t339);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t340 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                if (!t340) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_srcgr7vbgb5cqmg, 282, 87);
                gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                if (gm_pf1m4b8q4e78(cx, gm_sgankvlme129s, 2, gm_srcgr7vbgb5cqmg, 282, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t333) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t333)), GM_LD(uintptr_t, ((uint8_t *)&t333) + 8), gm_srcgr7vbgb5cqmg, 282, 9))) {
                    goto gm_lad;
                }
                if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                    goto gm_lad;
                }
                gm_buf b342 = {0};
                gm_format_spec(&b342, &gm_ty6groq37ram, ((uint8_t *)&t334), &(gm_spec){32, 2, 0, 0, 0, 0, 5u, -1});
                if (b342.len != 0) {
                    gm_b16_8 t343;
                    GM_ST(void *, (uint8_t *)&t343, (void *)(b342.data));
                    GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(b342.len));
                    gm_pfmaojsuds6a(cx, t343, gm_srcgr7vbgb5cqmg, 282, 9);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_buf_free(&b342);
                if (gm_pf1m4b8q4e78(cx, gm_st3ja0nqi37bv, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                    goto gm_lad;
                }
                gm_buf b344 = {0};
                gm_format_spec(&b344, &gm_ty6groq37ram, ((uint8_t *)&t335), &(gm_spec){32, 1, 0, 0, 0, 0, 5u, -1});
                if (b344.len != 0) {
                    gm_b16_8 t345;
                    GM_ST(void *, (uint8_t *)&t345, (void *)(b344.data));
                    GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(b344.len));
                    gm_pfmaojsuds6a(cx, t345, gm_srcgr7vbgb5cqmg, 282, 9);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_buf_free(&b344);
                if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                    goto gm_lad;
                }
                gm_buf b346 = {0};
                gm_format_spec(&b346, &gm_tybihgc69jv3, ((uint8_t *)&t341), &(gm_spec){32, 2, 0, 0, 0, 0, 6u, -1});
                if (b346.len != 0) {
                    gm_b16_8 t347;
                    GM_ST(void *, (uint8_t *)&t347, (void *)(b346.data));
                    GM_ST(uintptr_t, (uint8_t *)&t347 + 8, (uintptr_t)(b346.len));
                    gm_pfmaojsuds6a(cx, t347, gm_srcgr7vbgb5cqmg, 282, 9);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_buf_free(&b346);
                if (gm_pf1m4b8q4e78(cx, gm_sofs2uc27fjvk, 2, gm_srcgr7vbgb5cqmg, 282, 9)) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t348 = (uint8_t)((((uint8_t *)a4))[16] != 0);
        if (t348) {
            gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)a4));
            gm_l42 = t349;
        } else {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t350;
        gm_f_list_uvfu1k1((void *)&t350, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l43 = t350;
        gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
        gm_l44 = t351;
        gm_l45 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t352 = gm_l45;
                uint64_t t353 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                uint8_t t354 = (t352 < t353);
                if (!t354) {
                    goto gm_lb35;
                }
                {
                    void * t355 = GM_LD(void *, ((uint8_t *)&gm_l44));
                    uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                    uint64_t t357 = gm_l45;
                    {
                        {
                            gm_t2lqu8jsj511d t358 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)t355) + (uintptr_t)t357 * 16));
                            gm_l46 = t358;
                            void * t359 = (void *)((uint8_t *)&F.l43);
                            void * t360 = GM_LD(void *, ((uint8_t *)&F.l14));
                            uint64_t t361 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t t362 = GM_LD(uint64_t, ((uint8_t *)&gm_l46));
                            if ((uintptr_t)t362 >= (uintptr_t)(t361)) gm_trap_index((uintptr_t)t362, t361, gm_srcgr7vbgb5cqmg, 287, 36);
                            gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)t360) + (uintptr_t)t362 * 24));
                            gm_tnpifv20irnph t364;
                            GM_ST(gm_b16_8, ((uint8_t *)&t364 + 0), t363);
                            gm_tftb1odc739t5 t365 = gm_f_write_lmets0c(t359, t364);
                            gm_l67 = t365;
                            uint8_t t366 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                            if (!t366) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srcgr7vbgb5cqmg, 287, 9);
                            gm_l47 = ((uint64_t)0ULL);
                            void * t367 = GM_LD(void *, ((uint8_t *)&F.l14));
                            uint64_t t368 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t t369 = GM_LD(uint64_t, ((uint8_t *)&gm_l46));
                            uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                            uint64_t t371 = (uintptr_t)t369;
                            uint64_t t372 = (uintptr_t)t370;
                            if (t371 > t372 || t372 > (uintptr_t)t368) gm_trap_slice(t371, t372, t368, gm_srcgr7vbgb5cqmg, 289, 18);
                            uint64_t t373 = ((uintptr_t)t370) - (uintptr_t)t369;
                            gm_b16_8 t374;
                            GM_ST(void *, (uint8_t *)&t374, (void *)(((uint8_t *)t367 + (uintptr_t)t369 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(t373));
                            gm_l48 = t374;
                            gm_l49 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t375 = gm_l49;
                                    uint64_t t376 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                    uint8_t t377 = (t375 < t376);
                                    if (!t377) {
                                        goto gm_lb40;
                                    }
                                    {
                                        void * t378 = GM_LD(void *, ((uint8_t *)&gm_l48));
                                        uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                        uint64_t t380 = gm_l49;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l50), (((uint8_t *)t378) + (uintptr_t)t380 * 24), 24);
                                                void * t381 = (void *)((uint8_t *)&F.l43);
                                                uint32_t t382 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 16));
                                                uint32_t t383 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 20));
                                                gm_tcc1rq4n1avb2 t384;
                                                GM_ST(uint32_t, ((uint8_t *)&t384 + 0), t382);
                                                GM_ST(uint32_t, ((uint8_t *)&t384 + 4), t383);
                                                gm_tftb1odc739t5 t385 = gm_f_write_erog63d(t381, t384);
                                                gm_l68 = t385;
                                                uint8_t t386 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                                if (!t386) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcgr7vbgb5cqmg, 290, 13);
                                                uint32_t t387 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 20));
                                                uint8_t t388 = (t387 > ((uint32_t)0ULL));
                                                if (t388) {
                                                    uint64_t t389 = gm_l47;
                                                    uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                                                    if (o391) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 291, 29);
                                                    uint64_t t392 = r390;
                                                    gm_l47 = t392;
                                                }
                                                goto gm_lb43;
                                            }
gm_lb44:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 289, 9);
                                        }
gm_lb43:;
                                    }
gm_lb42:;
                                    uint64_t t393 = gm_l49;
                                    uint64_t r394; bool o395 = __builtin_add_overflow(t393, ((uint64_t)1ULL), &r394);
                                    uint64_t t396 = r394;
                                    gm_l49 = t396;
                                }
                            }
gm_lb40:;
                            void * t397 = (void *)((uint8_t *)&F.l43);
                            uint64_t t398 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                            uint64_t t399 = GM_LD(uint64_t, ((uint8_t *)&gm_l46));
                            uint64_t r400; bool o401 = __builtin_sub_overflow(t398, t399, &r400);
                            if (o401) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 293, 53);
                            uint64_t t402 = r400;
                            uint64_t t403 = gm_l47;
                            gm_t2lqu8jsj511d t404;
                            GM_ST(uint64_t, ((uint8_t *)&t404 + 0), t402);
                            GM_ST(uint64_t, ((uint8_t *)&t404 + 8), t403);
                            gm_tftb1odc739t5 t405 = gm_f_write_l7mh821(t397, t404);
                            gm_l69 = t405;
                            uint8_t t406 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                            if (!t406) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srcgr7vbgb5cqmg, 293, 9);
                            goto gm_lb38;
                        }
gm_lb39:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 286, 5);
                    }
gm_lb38:;
                }
gm_lb37:;
                uint64_t t407 = gm_l45;
                uint64_t r408; bool o409 = __builtin_add_overflow(t407, ((uint64_t)1ULL), &r408);
                uint64_t t410 = r408;
                gm_l45 = t410;
            }
        }
gm_lb35:;
    }
    {
        gm_b16_8 t411 = gm_l42;
        gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
        gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&t412));
        gm_ttln0d0hd70en t414 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t411, t413);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l70 = t414;
        uint8_t t415 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
        if (t415) {
        } else {
            gm_toe5fmr3vvf4d t416 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l70) + 4));
            gm_l51 = t416;
            gm_b16_8 t417 = gm_l42;
            gm_toe5fmr3vvf4d t418 = gm_l51;
            gm_b16_8 t419 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t418);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s7r1917rmmif7, 20, gm_srcgr7vbgb5cqmg, 296, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t417) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t417)), GM_LD(uintptr_t, ((uint8_t *)&t417) + 8), gm_srcgr7vbgb5cqmg, 296, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srcgr7vbgb5cqmg, 296, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t419) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t419)), GM_LD(uintptr_t, ((uint8_t *)&t419) + 8), gm_srcgr7vbgb5cqmg, 296, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcgr7vbgb5cqmg, 296, 9)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.ret = ((uint8_t)1);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_nqs6or3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vetgq3s F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* l */
    uint32_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tssfa0col9q01 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            gm_b16_8 t9 = gm_f_trim_fdrfe6i(t8);
            gm_f_parse_int_i2lnj6n((void *)&gm_l3, t9);
            uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (t10) {
                uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
                gm_l2 = t11;
            } else {
                gm_l2 = ((uint32_t)0ULL);
            }
            {
                for (;;) {
                    void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
                    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
                    uint8_t t15 = (t13 <= t14);
                    if (!t15) {
                        goto gm_lb3;
                    }
                    {
                        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
                        gm_tftb1odc739t5 t17 = gm_f_push_im9d4ro(t16, ((uint32_t)0ULL));
                        gm_l4 = t17;
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcgr7vbgb5cqmg, 169, 39);
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
            void * t20 = GM_LD(void *, ((uint8_t *)t19));
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t19) + 8));
            uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
            if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcgr7vbgb5cqmg, 170, 9);
            void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
            void * t24 = GM_LD(void *, ((uint8_t *)t23));
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
            uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
            if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcgr7vbgb5cqmg, 170, 27);
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t24) + (uintptr_t)t26 * 4));
            uint32_t t28 = gm_l2;
            uint32_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            uint32_t t31 = r29;
            GM_ST(uint32_t, (((uint8_t *)t20) + (uintptr_t)t22 * 4), t31);
            uint64_t t32 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 171, 9);
            uint64_t t35 = r33;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2), t35);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 167, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_instrument_mk8clvl F;
    void * gm_l0; /* s */
    void * gm_l1; /* scope */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* f */
    void * gm_l7; /* body */
    gm_tl874t2atnlh3 gm_l8; /* span */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* old */
    void * gm_l12; /* block */
    GM_STACK_CHECK(&F, 1223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdgfduqjheqdm gm_l13;
    void * gm_l14;
    uint8_t gm_l15;
    gm_t9805qtonbsag gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tq7s98ihonkju gm_l18;
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        gm_t4c7ipjj9s3fp t14;
        gm_f_hash_map_vqsve4u((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t15;
        gm_f_hash_map_70470jk((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t16;
        gm_f_list_1qnmubu((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t17;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t17 + 0), t16);
        gm_tqh23fthuf005 t18;
        GM_ST(void *, ((uint8_t *)&t18 + 0), t12);
        GM_ST(void *, ((uint8_t *)&t18 + 8), t13);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t18 + 16), t14);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t18 + 72), t15);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t18 + 128), t17);
        F.l2 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_f_counted_fns_j3p4pgn((void *)&F.l3, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_l5 = ((uint64_t)0ULL);
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t20) + 344) + 8));
    gm_l4 = t21;
    {
        for (;;) {
            uint64_t t22 = gm_l5;
            uint64_t t23 = gm_l4;
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t25 = gm_l5;
                        gm_l6 = t25;
                        void * t26 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t28 = gm_l6;
                        if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcgr7vbgb5cqmg, 58, 16);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
                        uint8_t t30 = (uint8_t)!t29;
                        if (t30) {
                            goto gm_lb3;
                        }
                        void * t31 = gm_l0;
                        uint64_t t32 = gm_l6;
                        uint32_t t33 = (uint32_t)t32;
                        gm_f_def_of_m512sbu((void *)&gm_l13, t31, t33);
                        void * t34 = GM_LD(void *, (((uint8_t *)&gm_l13) + 40));
                        gm_l14 = t34;
                        uint8_t t35 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l14)) != 0);
                        if (t35) {
                            void * t36 = GM_LD(void *, ((uint8_t *)&gm_l14));
                            gm_l7 = t36;
                        } else {
                            goto gm_lb3;
                        }
                        void * t37 = gm_l7;
                        {
                            {
                                uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
                                uint8_t t39 = (uint8_t)(t38 == ((uint8_t)46ULL));
                                if (!t39) {
                                    goto gm_lb7;
                                }
                                gm_l15 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l15 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 60, 12);
                        }
gm_lb6:;
                        uint8_t t40 = gm_l15;
                        if (t40) {
                            void * t41 = gm_l7;
                            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
                            gm_l8 = t42;
                            {
                                {
                                    void * t43 = (void *)((uint8_t *)&F.l2);
                                    gm_tl874t2atnlh3 t44 = gm_l8;
                                    gm_t9805qtonbsag t45 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t43, t44);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l16 = t45;
                                    uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l16))[4] != 0);
                                    if (!t46) {
                                        goto gm_lb10;
                                    }
                                    uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&gm_l16));
                                    gm_l9 = t47;
                                    {
                                        void * t48 = gm_l7;
                                        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
                                        void * t50 = gm_l7;
                                        gm_tl874t2atnlh3 t51 = gm_l8;
                                        void * t52 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &(*(gm_to0f5qn75cgg8 *)(((uint8_t *)t50) + 8)), t51);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l10 = t52;
                                    }
                                    {
                                        gm_trqfvmjtt4sij t53;
                                        gm_f_list_ir0h3tc_2((void *)&t53, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l11 = t53;
                                    }
                                    {
                                        void * t54 = (void *)((uint8_t *)&F.l11);
                                        void * t55 = gm_l0;
                                        uint32_t t56 = gm_l9;
                                        gm_tl874t2atnlh3 t57 = gm_l8;
                                        gm_trcmmd12cefb1 t58;
                                        gm_f_cover_stmt_bp29hrl((void *)&t58, (void *)NULL, cx, NULL, t55, t56, t57);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t59 = gm_f_push_d6c76ud_2(t54, &t58);
                                        gm_l17 = t59;
                                    }
                                    uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                    if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcgr7vbgb5cqmg, 66, 17);
                                    {
                                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                        void * t62 = gm_l10;
                                        gm_tl874t2atnlh3 t63 = gm_l8;
                                        gm_tgabaikr08mjg t64;
                                        GM_ST(uint32_t, (uint8_t *)&t64 + 36, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t61);
                                        GM_ST(void *, ((uint8_t *)&t64 + 16), t62);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t64 + 24), t63);
                                        gm_tq7s98ihonkju t65 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t64, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kp3nl0er6il)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l18 = t65;
                                    }
                                    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcgr7vbgb5cqmg, 67, 26);
                                    void * t67 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
                                    gm_l12 = t67;
                                    {
                                        void * t68 = gm_l7;
                                        void * t69 = gm_l7;
                                        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t69));
                                        void * t71 = gm_l12;
                                        gm_tl874t2atnlh3 t72 = gm_l8;
                                        gm_to0f5qn75cgg8 t73;
                                        memset(&t73, 0, sizeof t73);
                                        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)46ULL));
                                        GM_ST(void *, ((uint8_t *)&t73 + 8), t71);
                                        gm_tsf6i2agfnpva t74;
                                        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t74 + 84, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t74 + 0), t70);
                                        GM_ST(gm_to0f5qn75cgg8, ((uint8_t *)&t74 + 8), t73);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t74 + 72), t72);
                                        GM_ST(gm_tsf6i2agfnpva, ((uint8_t *)t68), t74);
                                    }
                                    goto gm_lb9;
                                }
gm_lb10:;
                            }
gm_lb9:;
                        }
                        void * t75 = (void *)((uint8_t *)&F.l2);
                        void * t76 = gm_l7;
                        gm_b0_1 t77 = gm_f_visit_mnqi05r((void *)NULL, cx, NULL, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 57, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t78 = gm_l5;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            uint64_t t81 = r79;
            gm_l5 = t81;
        }
    }
gm_lb1:;
    void * t82 = gm_l0;
    uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)&F.l2) + 128) + 8));
    uint32_t t84 = (uint32_t)t83;
    GM_ST(uint32_t, (((uint8_t *)t82) + 2280), t84);
    {
        memmove(gm_out, (((uint8_t *)&F.l2) + 128), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pad_o3a3nuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_pad_o3a3nuu F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* width */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 251u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    void * t8 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t9 = gm_l0;
    gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
    gm_l5 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgr7vbgb5cqmg, 213, 5);
    gm_b16_8 t12 = gm_l0;
    uint64_t t13 = gm_f_chars_232hkk6(t12);
    gm_l4 = t13;
    uint64_t t14 = gm_l1;
    gm_l3 = t14;
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = gm_l3;
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t19 = gm_f_push_t83rg2m(t18, ((uint8_t)32ULL));
                        gm_l6 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcgr7vbgb5cqmg, 214, 32);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 214, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l4;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l4 = t24;
        }
    }
gm_lb1:;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    F.ret = t26;
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
inline __attribute__((aligned(4))) uint8_t gm_f_path_less_il538gm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_path_less_il538gm F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 526u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t9;
    }
    gm_b16_8 t10 = gm_l0;
    gm_l3 = t10;
    {
        gm_node wn11 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlvedue473cr, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t14 = gm_l3;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
        gm_b0_1 t16 = gm_f_split_obtinjv_st80pr1((void *)NULL, (&wn11), NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_ll13;
        }
    }
    goto gm_ls17;
gm_ll13:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we12; }
    goto gm_lad;
gm_ls17:;
gm_lb1:;
we12:;
    {
        gm_trqfvmjtt4sij t18;
        gm_f_list_epvducg((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t18;
    }
    gm_b16_8 t19 = gm_l1;
    gm_l5 = t19;
    {
        gm_node wn20 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlqu6cf845ie, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t23 = gm_l5;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
        gm_b0_1 t25 = gm_f_split_obtinjv_sq3203i((void *)NULL, (&wn20), NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_ll22;
        }
    }
    goto gm_ls26;
gm_ll22:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we21; }
    goto gm_lad;
gm_ls26:;
gm_lb2:;
we21:;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint64_t t29 = (t27 < t28 ? t27 : t28);
    gm_l6 = t29;
    gm_l8 = ((uint64_t)0ULL);
    uint64_t t30 = gm_l6;
    gm_l7 = t30;
    {
        for (;;) {
            uint64_t t32 = gm_l8;
            uint64_t t33 = gm_l7;
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb3;
            }
            {
                {
                    {
                        uint64_t t35 = gm_l8;
                        gm_l9 = t35;
                        void * t36 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        uint64_t t38 = gm_l9;
                        if ((uintptr_t)t38 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t38, t37, gm_srcgr7vbgb5cqmg, 190, 12);
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t36) + (uintptr_t)t38 * 16));
                        void * t40 = GM_LD(void *, ((uint8_t *)&F.l4));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                        uint64_t t42 = gm_l9;
                        if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcgr7vbgb5cqmg, 190, 30);
                        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t40) + (uintptr_t)t42 * 16));
                        uint8_t t44 = (!gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t43)));
                        if (t44) {
                            void * t45 = GM_LD(void *, ((uint8_t *)&F.l2));
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                            uint64_t t47 = gm_l9;
                            if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_srcgr7vbgb5cqmg, 190, 61);
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)t47 * 16));
                            void * t49 = GM_LD(void *, ((uint8_t *)&F.l4));
                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                            uint64_t t51 = gm_l9;
                            if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_srcgr7vbgb5cqmg, 190, 73);
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t49) + (uintptr_t)t51 * 16));
                            uint8_t t53 = gm_f_bytes_less_r33ee4s(t48, t52);
                            F.ret = t53;
                            goto gm_fret;
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 189, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t54 = gm_l8;
            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
            uint64_t t57 = r55;
            gm_l8 = t57;
        }
    }
gm_lb3:;
    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t60 = (t58 < t59);
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
extern __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_point_8frh2ab(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_point_8frh2ab F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* canon */
    uint8_t gm_l4; /* yes */
    uint8_t gm_l5; /* inside */
    uint32_t gm_l6; /* line */
    gm_tcc1rq4n1avb2 gm_l7; /* key */
    uint32_t gm_l8; /* id */
    uint32_t gm_l9; /* id */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    uint8_t gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tcc1rq4n1avb2 gm_l14;
    gm_t9805qtonbsag gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t6 = (gm_eqff85ac10o3(((uint8_t *)&t4), ((uint8_t *)&t5)));
    if (t6) {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 72);
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_tqs5p4n2tq19v t11 = gm_f_get_dk5gh28(t9, t10);
    gm_l10 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
    if (t12) {
        uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
        gm_l5 = t13;
    } else {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, ((uint8_t *)t14));
        void * t16 = GM_LD(void *, ((uint8_t *)t15));
        void * t17 = GM_LD(void *, ((uint8_t *)t16));
        uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        void * t19 = gm_f_file_o15fqku(t17, t18);
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
        gm_l2 = t20;
        gm_b16_8 t21 = gm_l2;
        gm_f_canonicalize_epo7cln((void *)&gm_l11, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
        if (t22) {
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l3 = t23;
        } else {
            gm_b16_8 t24 = gm_l2;
            gm_l3 = t24;
        }
        gm_b16_8 t25 = gm_l3;
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)t27));
        uint8_t t29 = gm_f_under_rb6c23o(t25, t28);
        if (t29) {
            gm_b16_8 t30 = gm_l3;
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 16));
            uint8_t t34 = gm_f_under_rb6c23o(t30, t33);
            uint8_t t35 = (uint8_t)!t34;
            gm_l12 = t35;
        } else {
            gm_l12 = ((uint8_t)0);
        }
        uint8_t t36 = gm_l12;
        gm_l4 = t36;
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 72);
        uint32_t t39 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        uint8_t t40 = gm_l4;
        gm_tftb1odc739t5 t41 = gm_f_put_v6g5pup(t38, t39, t40);
        gm_l13 = t41;
        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcgr7vbgb5cqmg, 110, 9);
        uint8_t t43 = gm_l4;
        gm_l5 = t43;
    }
    uint8_t t44 = gm_l5;
    gm_l5 = t44;
    uint8_t t45 = gm_l5;
    uint8_t t46 = (uint8_t)!t45;
    if (t46) {
        gm_t9805qtonbsag t47;
        memset(&t47, 0, sizeof t47);
        F.ret = t47;
        goto gm_fret;
    }
    void * t48 = gm_l0;
    void * t49 = GM_LD(void *, ((uint8_t *)t48));
    void * t50 = GM_LD(void *, ((uint8_t *)t49));
    void * t51 = GM_LD(void *, ((uint8_t *)t50));
    uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t53 = gm_f_file_o15fqku(t51, t52);
    uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t55 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t53, t54));
    gm_l14 = t55;
    uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
    gm_l6 = t56;
    uint32_t t57 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint32_t t58 = gm_l6;
    gm_tcc1rq4n1avb2 t59;
    GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t57);
    GM_ST(uint32_t, ((uint8_t *)&t59 + 4), t58);
    gm_l7 = t59;
    {
        {
            void * t60 = gm_l0;
            void * t61 = (void *)(((uint8_t *)t60) + 16);
            gm_tcc1rq4n1avb2 t62 = gm_l7;
            gm_t9805qtonbsag t63 = gm_f_get_78dnvqp(t61, t62);
            gm_l15 = t63;
            uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
            if (!t64) {
                goto gm_lb2;
            }
            uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
            gm_l8 = t65;
            uint32_t t66 = gm_l8;
            gm_t9805qtonbsag t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t67), t66);
            ((uint8_t *)&t67)[4] = 1;
            F.ret = t67;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t68 = gm_l0;
    uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)t68) + 128) + 8));
    uint32_t t70 = (uint32_t)t69;
    gm_l9 = t70;
    void * t71 = gm_l0;
    void * t72 = (void *)(((uint8_t *)t71) + 128);
    uint32_t t73 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint32_t t74 = gm_l6;
    gm_tcc1rq4n1avb2 t75;
    GM_ST(uint32_t, ((uint8_t *)&t75 + 0), t73);
    GM_ST(uint32_t, ((uint8_t *)&t75 + 4), t74);
    gm_tftb1odc739t5 t76 = gm_f_push_lstf2b8(t72, t75);
    gm_l16 = t76;
    uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcgr7vbgb5cqmg, 118, 5);
    void * t78 = gm_l0;
    void * t79 = (void *)(((uint8_t *)t78) + 16);
    gm_tcc1rq4n1avb2 t80 = gm_l7;
    uint32_t t81 = gm_l9;
    gm_tftb1odc739t5 t82 = gm_f_put_a7l7vun(t79, t80, t81);
    gm_l17 = t82;
    uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcgr7vbgb5cqmg, 119, 5);
    uint32_t t84 = gm_l9;
    gm_t9805qtonbsag t85;
    GM_ST(uint8_t, (uint8_t *)&t85 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t85 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t85), t84);
    ((uint8_t *)&t85)[4] = 1;
    F.ret = t85;
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
inline __attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_scope_4n0q0tq F;
    gm_b16_8 gm_l0; /* input */
    gm_b16_8 gm_l1; /* canon */
    gm_b16_8 gm_l2; /* dir */
    gm_b16_8 gm_l3; /* root */
    gm_b16_8 gm_l4; /* std */
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l5;
    gm_b16_8 gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    {
        gm_b16_8 t2 = gm_l0;
        gm_f_canonicalize_epo7cln((void *)&gm_l5, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_l1 = t4;
    } else {
        gm_b16_8 t5 = gm_l0;
        gm_l1 = t5;
    }
    gm_b16_8 t6 = gm_l1;
    uint8_t t7 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t7) {
        gm_b16_8 t8 = gm_l1;
        gm_l2 = t8;
    } else {
        gm_b16_8 t9 = gm_l1;
        gm_f_parent_lk0iero((void *)&gm_l7, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
        if (t10) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_l6 = t11;
        } else {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            gm_l6 = t12;
        }
        gm_b16_8 t13 = gm_l6;
        gm_l2 = t13;
    }
    {
        gm_b16_8 t14 = gm_l2;
        gm_f_find_root_kjnm6hf((void *)&gm_l8, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t15) {
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l3 = t16;
    } else {
        gm_b16_8 t17 = gm_l2;
        gm_l3 = t17;
    }
    gm_b16_8 t18 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t18;
    {
        gm_b16_8 t19 = gm_l3;
        gm_b16_8 t20 = gm_l4;
        gm_f_canonicalize_epo7cln((void *)&gm_l10, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
        if (t21) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l9 = t22;
        } else {
            gm_b16_8 t23 = gm_l4;
            gm_l9 = t23;
        }
        gm_b16_8 t24 = gm_l9;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 0), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 16), t24);
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_span_slh12dn F;
    gm_trcmmd12cefb1 gm_l0; /* st */
    void * gm_l1; /* x */
    void * gm_l2; /* init */
    void * gm_l3; /* place */
    void * gm_l4; /* value */
    void * gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_toceffb697p0u gm_l6;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a0) + 8) + 8)) != 0);
            if (t12) {
                goto gm_lb2;
            }
            gm_toceffb697p0u t13;
            memset(&t13, 0, sizeof t13);
            gm_l6 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb3;
            }
            uint8_t t16 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a0) + 8) + 8)) != 0);
            if (!t16) {
                goto gm_lb3;
            }
            void * t17 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 8));
            gm_l1 = t17;
            void * t18 = gm_l1;
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
            gm_toceffb697p0u t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[12] = 1;
            gm_l6 = t20;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb4;
            }
            void * t23 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 16));
            gm_l2 = t23;
            void * t24 = gm_l2;
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 72));
            gm_toceffb697p0u t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[12] = 1;
            gm_l6 = t26;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
            if (!t28) {
                goto gm_lb5;
            }
            void * t29 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l3 = t29;
            void * t30 = gm_l3;
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            gm_toceffb697p0u t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[12] = 1;
            gm_l6 = t32;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
            if (!t34) {
                goto gm_lb6;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 16));
            gm_l4 = t35;
            void * t36 = gm_l4;
            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
            gm_toceffb697p0u t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[12] = 1;
            gm_l6 = t38;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)4ULL));
            if (!t40) {
                goto gm_lb7;
            }
            void * t41 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l5 = t41;
            void * t42 = gm_l5;
            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
            gm_toceffb697p0u t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t44), t43);
            ((uint8_t *)&t44)[12] = 1;
            gm_l6 = t44;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)5ULL));
            if (!t46) {
                goto gm_lb8;
            }
            gm_toceffb697p0u t47;
            memset(&t47, 0, sizeof t47);
            gm_l6 = t47;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 149, 12);
    }
gm_lb1:;
    gm_toceffb697p0u t48 = gm_l6;
    F.ret = t48;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn_c(gm_trcmmd12cefb1 a0) { return gm_f_stmt_span_slh12dn(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_under_rb6c23o(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_under_rb6c23o F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* dir */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t8 = ((t6 == t7));
    if (t8) {
        gm_l3 = ((uint8_t)1);
    } else {
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
        uint8_t t11 = gm_f_ends_with_biriu5l(t9, t10);
        gm_l3 = t11;
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcgr7vbgb5cqmg, 33, 54);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
        uint8_t t17 = ((t16 == ((uint8_t)47ULL)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_visit_mnqi05r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_visit_mnqi05r F;
    void * gm_l1; /* e */
    void * gm_l2; /* b */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    void * t6 = gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)46ULL));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l2 = t9;
            void * t10 = F.l0;
            void * t11 = gm_l2;
            gm_b0_1 t12 = gm_f_block_u0oqu8p((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)48ULL));
            if (!t14) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t6) + 8) + 8), 40);
            void * t15 = F.l0;
            void * t16 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
            gm_b0_1 t17 = gm_f_block_u0oqu8p((void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 124, 5);
    }
gm_lb1:;
    void * t18 = gm_l1;
    gm_closure t19 = ((gm_closure){(void *)gm_f_fn_g9dfnu7, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t18, t19);
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

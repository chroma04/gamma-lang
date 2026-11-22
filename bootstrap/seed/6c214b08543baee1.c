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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binop_nrsq3mp {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_bytes_equal_cksjlgj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_mflafbf {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_contains_nugmipn {
    gm_frame h;
    gm_t2jeg3bg7sfqa l6; /* s */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_equal_e5vf82h {
    gm_frame h;
    gm_t2jeg3bg7sfqa l20; /* sa */
    gm_t2jeg3bg7sfqa l21; /* sb */
    gm_tpautr4br7h3l l39; /* def */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_9qvncot {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aib5qn5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b946o7j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bvot0a7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dmukbli {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gd3p27d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h8e7ps1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kfcf8tk {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mq7e94u {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra7pont {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vno937p {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vqmfrgb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_invalid_tag_1gif354 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_invalid_tag_text_f3l5hi7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_cmp_r2i4hv9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_less_nbfe87l {
    gm_frame h;
    gm_t2jeg3bg7sfqa l14; /* sa */
    gm_t2jeg3bg7sfqa l15; /* sb */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_op_sym_6olbepb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_seq_of_ifahtjs {
    gm_frame h;
    gm_tpekhfs32op0f l10; /* e */
    gm_tmc91gv7n9c0i ret;
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
struct gm_fr_unop_qamfooj {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
inline __attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b3uc2q7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gd3p27d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mnuav4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mq7e94u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s121p29851qjl[39];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2aq94adogrjl[35];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s3ijb9gbhdbib[27];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4du6i4h6s7qo[32];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4po5fk785g2b[37];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s7ah1q5ah26rj[12];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa2pnk1jujp5p[23];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sbf8eactntf1g[21];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_shes0dtqt9sqq[28];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcj10g653a00lt[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssa1hve63tj2e[21];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_sufif7dqc1pm3[17];
extern const gm_tmpl gm_tpl46q9qc00shi4;
extern const gm_tmpl gm_tplaq7jlj8i0s87;
extern const gm_tmpl gm_tplcnvlhj00qs0h;
extern const gm_tmpl gm_tpliuif0nifmui1;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tpllaurt6qemv8q;
extern const gm_tmpl gm_tplmn77qas0g7s4;
extern const gm_tmpl gm_tpln38mjgenps81;
extern const gm_tmpl gm_tploui9f30uh460;
extern const gm_tmpl gm_tplr2tuff1mvdst;
extern const gm_tmpl gm_tplvl1jqbb33ag2;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
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
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0) {
    struct gm_fr_format_9qvncot F;
    gm_tpl3q6e8s9qo1 gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tpl3q6e8s9qo1 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_b3uc2q7((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aib5qn5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s121p29851qjl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr2tuff1mvdst), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b946o7j F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shes0dtqt9sqq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmn77qas0g7s4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bvot0a7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2aq94adogrjl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(34));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln38mjgenps81), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dmukbli F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4du6i4h6s7qo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcnvlhj00qs0h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gd3p27d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gd3p27d F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa1hve63tj2e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplaq7jlj8i0s87), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h8e7ps1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6st5mdtp49u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvl1jqbb33ag2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kfcf8tk F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3ijb9gbhdbib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tploui9f30uh460), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mq7e94u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mq7e94u F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4po5fk785g2b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpliuif0nifmui1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_ra7pont F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tsn2omssg6cpf t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_mnuav4d((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vno937p F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sufif7dqc1pm3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl46q9qc00shi4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vqmfrgb F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbf8eactntf1g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllaurt6qemv8q), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_nullable_pointer_nkie06s(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t6 = (t5 > ((uint64_t)1ULL));
    if (t6) {
        void * t7 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l3 = t9;
    } else {
        gm_l3 = ((uint64_t)0ULL);
    }
    uint64_t t10 = gm_l3;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t10);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l3;
    uint8_t t13 = gm_f_aligned_q6sor4h(t11, t12);
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                void * t17 = gm_f_view_qalkbv1(t14, t15, t16, ((uint8_t)0));
                gm_l5 = t17;
                uint8_t t18 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t18) {
                    goto gm_lb4;
                }
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t19;
                void * t20 = gm_l4;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_f_us_cuv01fp(t21);
                uint64_t t23 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t24);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l2;
        uint64_t t28 = gm_l3;
        gm_tpekhfs32op0f t29;
        gm_f_fault_u31b92l((void *)&t29, t25, t26, t27, t28, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l6 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    uint64_t t24 = gm_l1;
    void * t25 = gm_l5;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
    uint64_t r27; bool o28 = __builtin_sub_overflow(t24, t26, &r27);
    if (o28) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t29 = r27;
    gm_l7 = t29;
    uint64_t t30 = gm_l7;
    void * t31 = gm_l5;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 16));
    unsigned __int128 n33 = (unsigned __int128)t32;
    if (n33 >= 64) gm_trap_shift((uint64_t)n33, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t34 = (uint64_t)(t30 >> (int)n33);
    gm_l8 = t34;
    void * t35 = gm_l6;
    uint64_t t36 = gm_l8;
    uint64_t t37 = gm_f_us_cuv01fp(t36);
    void * t38 = (void *)(((uint8_t *)t35) + (uintptr_t)t37 * 24);
    gm_l9 = t38;
    uint64_t t39 = gm_l7;
    uint64_t t40 = gm_l8;
    void * t41 = gm_l5;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 16));
    unsigned __int128 n43 = (unsigned __int128)t42;
    if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r44 = (uint64_t)((uint64_t)t40 << (int)n43);
    bool o45 = (uint64_t)(r44 >> (int)n43) != t40;
    if (o45) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t46 = r44;
    uint64_t r47; bool o48 = __builtin_sub_overflow(t39, t46, &r47);
    if (o48) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t49 = r47;
    gm_l10 = t49;
    void * t50 = gm_l9;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 17));
    uint8_t t52 = (uint8_t)!t51;
    if (t52) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t53 = gm_l2;
        void * t54 = gm_l9;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
        uint8_t t56 = (t53 > t55);
        gm_l13 = t56;
    }
    uint8_t t57 = gm_l13;
    if (t57) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint64_t t58 = gm_l10;
        void * t59 = gm_l9;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
        uint64_t t61 = gm_l2;
        uint64_t r62; bool o63 = __builtin_sub_overflow(t60, t61, &r62);
        if (o63) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t64 = r62;
        uint8_t t65 = (t58 > t64);
        gm_l12 = t65;
    }
    uint8_t t66 = gm_l12;
    if (t66) {
        gm_l11 = ((uint8_t)1);
    } else {
        uint8_t t67 = gm_l3;
        if (t67) {
            void * t68 = gm_l9;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 18));
            uint8_t t70 = (uint8_t)!t69;
            gm_l14 = t70;
        } else {
            gm_l14 = ((uint8_t)0);
        }
        uint8_t t71 = gm_l14;
        gm_l11 = t71;
    }
    uint8_t t72 = gm_l11;
    if (t72) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t73 = gm_l9;
    void * t74 = GM_LD(void *, ((uint8_t *)t73));
    uint64_t t75 = gm_l10;
    uint64_t t76 = gm_f_us_cuv01fp(t75);
    void * t77 = (void *)(((uint8_t *)t74) + (uintptr_t)t76 * 1);
    void * t78 = t77;
    F.ret = t78;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_binop_nrsq3mp F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* rt */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* dst */
    uint8_t gm_l9; /* v */
    double gm_l10; /* p */
    double gm_l11; /* q */
    double gm_l12; /* r */
    uint64_t gm_l13; /* n */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    uint64_t gm_l15; /* esz */
    uint8_t gm_l16; /* scalar_r */
    gm_t88rcucg1b7uk gm_l17; /* ety */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* xs */
    gm_b16_8 gm_l23; /* ys */
    gm_b16_8 gm_l24; /* ds */
    gm_t88rcucg1b7uk gm_l25; /* u */
    GM_STACK_CHECK(&F, 2363u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    gm_t1ec8br9ddkav gm_l26;
    uint8_t gm_l27;
    gm_t53at80pfdus3 gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_t53at80pfdus3 gm_l30;
    uint8_t gm_l31;
    gm_tcfa6l7reoosn gm_l32;
    gm_tkn42u7na3t81 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    {
        gm_ts6oqppjh0ba3 t30 = gm_l2;
        uint8_t t31 = gm_f_is_cmp_r2i4hv9(t30);
        if (t31) {
            {
                void * t32 = gm_l0;
                gm_tl874t2atnlh3 t33 = gm_l1;
                gm_ts6oqppjh0ba3 t34 = gm_l2;
                gm_t88rcucg1b7uk t35 = gm_l3;
                gm_b16_8 t36 = gm_l6;
                gm_b16_8 t37 = gm_l7;
                gm_f_cmp_op_mflafbf((void *)&gm_l26, (void *)NULL, cx, NULL, t32, t33, t34, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (t38) {
            } else {
                gm_t76123gfet73g t39 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l26) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t39);
                goto gm_fdone;
            }
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
            gm_l9 = t40;
            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l8));
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t42, gm_srcj10g653a00lt, 36, 9);
            uint8_t t43 = gm_l9;
            if (t43) {
                gm_l27 = ((uint8_t)1ULL);
            } else {
                gm_l27 = ((uint8_t)0ULL);
            }
            uint8_t t44 = gm_l27;
            GM_ST(uint8_t, (((uint8_t *)t41) + (uintptr_t)((uint64_t)0ULL) * 1), t44);
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                goto gm_fdone;
            }
        }
        void * t45 = gm_l0;
        gm_t88rcucg1b7uk t46 = gm_l3;
        gm_f_kind_htv6093((void *)&gm_l28, t45, GM_CONV(gm_csguankko4lmhd, t46));
        {
            {
                {
                    {
                        {
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
                            if (!t48) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                            if (!t50) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                            if (!t52) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t53 = gm_l0;
                    gm_tl874t2atnlh3 t54 = gm_l1;
                    gm_ts6oqppjh0ba3 t55 = gm_l2;
                    gm_t88rcucg1b7uk t56 = gm_l3;
                    gm_t88rcucg1b7uk t57 = gm_l4;
                    gm_t88rcucg1b7uk t58 = gm_l5;
                    gm_b16_8 t59 = gm_l6;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                    gm_b16_8 t61 = gm_l7;
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                    gm_b16_8 t63 = gm_l8;
                    gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t53, t54, t55, t56, t57, t58, t60, t62, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
                    if (!t65) {
                        goto gm_lb7;
                    }
                    void * t66 = gm_l0;
                    gm_t88rcucg1b7uk t67 = gm_l3;
                    gm_b16_8 t68 = gm_l6;
                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                    double t70 = gm_f_get_float_grmhlm4(t66, GM_CONV(gm_csguankko4lmhd, t67), t69);
                    gm_l10 = t70;
                    void * t71 = gm_l0;
                    gm_t88rcucg1b7uk t72 = gm_l3;
                    gm_b16_8 t73 = gm_l7;
                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                    double t75 = gm_f_get_float_grmhlm4(t71, GM_CONV(gm_csguankko4lmhd, t72), t74);
                    gm_l11 = t75;
                    {
                        {
                            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)0ULL));
                            if (!t77) {
                                goto gm_lb9;
                            }
                            double t78 = gm_l10;
                            double t79 = gm_l11;
                            double t80 = (t78 + t79);
                            gm_l12 = t80;
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)1ULL));
                            if (!t82) {
                                goto gm_lb10;
                            }
                            double t83 = gm_l10;
                            double t84 = gm_l11;
                            double t85 = (t83 - t84);
                            gm_l12 = t85;
                            goto gm_lb8;
                        }
gm_lb10:;
                        {
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)2ULL));
                            if (!t87) {
                                goto gm_lb11;
                            }
                            double t88 = gm_l10;
                            double t89 = gm_l11;
                            double t90 = (t88 * t89);
                            gm_l12 = t90;
                            goto gm_lb8;
                        }
gm_lb11:;
                        {
                            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)4ULL));
                            if (!t92) {
                                goto gm_lb12;
                            }
                            double t93 = gm_l10;
                            double t94 = gm_l11;
                            double t95 = (t93 / t94);
                            gm_l12 = t95;
                            goto gm_lb8;
                        }
gm_lb12:;
                        {
                            void * t96 = gm_l0;
                            gm_tl874t2atnlh3 t97 = gm_l1;
                            gm_ts6oqppjh0ba3 t98 = gm_l2;
                            gm_b16_8 t99 = gm_f_op_sym_6olbepb(t98);
                            gm_tnpifv20irnph t100;
                            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
                            gm_f_format_aib5qn5((void *)&gm_l29, (void *)NULL, cx, NULL, t100);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcj10g653a00lt, 49, 43);
                            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                            gm_t76123gfet73g t104;
                            gm_f_trap_qh84l9d((void *)&t104, (void *)NULL, cx, NULL, t96, t97, t103);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t104);
                            goto gm_fdone;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 44, 18);
                    }
gm_lb8:;
                    void * t105 = gm_l0;
                    gm_t88rcucg1b7uk t106 = gm_l3;
                    double t107 = gm_l12;
                    gm_b16_8 t108 = gm_l8;
                    gm_f_put_float_fiad72d(t105, GM_CONV(gm_csguankko4lmhd, t106), t107, t108);
                    goto gm_lb1;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)11ULL));
                    if (!t110) {
                        goto gm_lb14;
                    }
                    uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                    gm_l13 = t111;
                    gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l28) + 8) + 8));
                    gm_l14 = t112;
                    void * t113 = gm_l0;
                    gm_t88rcucg1b7uk t114 = gm_l14;
                    uint64_t t115 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t113, t114);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t116 = gm_f_us_cuv01fp(t115);
                    gm_l15 = t116;
                    {
                        void * t117 = gm_l0;
                        gm_t88rcucg1b7uk t118 = gm_l4;
                        gm_f_kind_htv6093((void *)&gm_l30, t117, GM_CONV(gm_csguankko4lmhd, t118));
                    }
                    {
                        {
                            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)11ULL));
                            if (!t120) {
                                goto gm_lb16;
                            }
                            gm_l16 = ((uint8_t)0);
                            goto gm_lb15;
                        }
gm_lb16:;
                        {
                            gm_l16 = ((uint8_t)1);
                            goto gm_lb15;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 56, 25);
                    }
gm_lb15:;
                    uint8_t t121 = gm_l16;
                    if (t121) {
                        gm_t88rcucg1b7uk t122 = gm_l4;
                        gm_l17 = t122;
                    } else {
                        gm_t88rcucg1b7uk t123 = gm_l14;
                        gm_l17 = t123;
                    }
                    {
                        void * t124 = gm_l0;
                        gm_t88rcucg1b7uk t125 = gm_l14;
                        gm_t53at80pfdus3 t126;
                        gm_f_kind_htv6093((void *)&t126, t124, GM_CONV(gm_csguankko4lmhd, t125));
                        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t126) + 0));
                        uint8_t t128 = (uint8_t)((t127 == ((uint8_t)4ULL)));
                        gm_t53at80pfdus3 t129;
                        memset(&t129, 0, sizeof t129);
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)4ULL));
                        gm_l18 = t128;
                    }
                    gm_l20 = ((uint64_t)0ULL);
                    uint64_t t130 = gm_l13;
                    uint64_t t131 = gm_f_us_cuv01fp(t130);
                    gm_l19 = t131;
                    {
                        for (;;) {
                            uint64_t t132 = gm_l20;
                            uint64_t t133 = gm_l19;
                            uint8_t t134 = (t132 < t133);
                            if (!t134) {
                                goto gm_lb18;
                            }
                            {
                                {
                                    {
                                        uint64_t t135 = gm_l20;
                                        gm_l21 = t135;
                                        void * t136 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t138 = gm_l21;
                                        uint64_t t139 = gm_l15;
                                        uint64_t r140; bool o141 = __builtin_mul_overflow(t138, t139, &r140);
                                        if (o141) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 25);
                                        uint64_t t142 = r140;
                                        uint64_t t143 = gm_l21;
                                        uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                                        if (o145) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 60, 35);
                                        uint64_t t146 = r144;
                                        uint64_t t147 = gm_l15;
                                        uint64_t r148; bool o149 = __builtin_mul_overflow(t146, t147, &r148);
                                        if (o149) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 34);
                                        uint64_t t150 = r148;
                                        uint64_t t151 = (uintptr_t)t142;
                                        uint64_t t152 = (uintptr_t)t150;
                                        if (t151 > t152 || t152 > (uintptr_t)t137) gm_trap_slice(t151, t152, t137, gm_srcj10g653a00lt, 60, 23);
                                        uint64_t t153 = ((uintptr_t)t150) - (uintptr_t)t142;
                                        gm_b16_8 t154;
                                        GM_ST(void *, (uint8_t *)&t154, (void *)(((uint8_t *)t136 + (uintptr_t)t142 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(t153));
                                        gm_l22 = t154;
                                        uint8_t t155 = gm_l16;
                                        if (t155) {
                                            gm_b16_8 t156 = gm_l7;
                                            gm_l23 = t156;
                                        } else {
                                            void * t157 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                            uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                            uint64_t t159 = gm_l21;
                                            uint64_t t160 = gm_l15;
                                            uint64_t r161; bool o162 = __builtin_mul_overflow(t159, t160, &r161);
                                            if (o162) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 50);
                                            uint64_t t163 = r161;
                                            uint64_t t164 = gm_l21;
                                            uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                                            if (o166) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 61, 60);
                                            uint64_t t167 = r165;
                                            uint64_t t168 = gm_l15;
                                            uint64_t r169; bool o170 = __builtin_mul_overflow(t167, t168, &r169);
                                            if (o170) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 59);
                                            uint64_t t171 = r169;
                                            uint64_t t172 = (uintptr_t)t163;
                                            uint64_t t173 = (uintptr_t)t171;
                                            if (t172 > t173 || t173 > (uintptr_t)t158) gm_trap_slice(t172, t173, t158, gm_srcj10g653a00lt, 61, 48);
                                            uint64_t t174 = ((uintptr_t)t171) - (uintptr_t)t163;
                                            gm_b16_8 t175;
                                            GM_ST(void *, (uint8_t *)&t175, (void *)(((uint8_t *)t157 + (uintptr_t)t163 * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(t174));
                                            gm_l23 = t175;
                                        }
                                        void * t176 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        uint64_t t178 = gm_l21;
                                        uint64_t t179 = gm_l15;
                                        uint64_t r180; bool o181 = __builtin_mul_overflow(t178, t179, &r180);
                                        if (o181) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 27);
                                        uint64_t t182 = r180;
                                        uint64_t t183 = gm_l21;
                                        uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                        if (o185) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 62, 37);
                                        uint64_t t186 = r184;
                                        uint64_t t187 = gm_l15;
                                        uint64_t r188; bool o189 = __builtin_mul_overflow(t186, t187, &r188);
                                        if (o189) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 36);
                                        uint64_t t190 = r188;
                                        uint64_t t191 = (uintptr_t)t182;
                                        uint64_t t192 = (uintptr_t)t190;
                                        if (t191 > t192 || t192 > (uintptr_t)t177) gm_trap_slice(t191, t192, t177, gm_srcj10g653a00lt, 62, 23);
                                        uint64_t t193 = ((uintptr_t)t190) - (uintptr_t)t182;
                                        gm_b16_8 t194;
                                        GM_ST(void *, (uint8_t *)&t194, (void *)(((uint8_t *)t176 + (uintptr_t)t182 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(t193));
                                        gm_l24 = t194;
                                        uint8_t t195 = gm_l18;
                                        if (t195) {
                                            void * t196 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                            uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t197)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t197, gm_srcj10g653a00lt, 65, 21);
                                            {
                                                {
                                                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)7ULL));
                                                    if (!t199) {
                                                        goto gm_lb24;
                                                    }
                                                    void * t200 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                    uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t201)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t201, gm_srcj10g653a00lt, 66, 36);
                                                    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)t200) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    void * t203 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t204)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t204, gm_srcj10g653a00lt, 66, 44);
                                                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)t203) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    uint8_t t206 = (uint8_t)(t202 & t205);
                                                    gm_l31 = t206;
                                                    goto gm_lb23;
                                                }
gm_lb24:;
                                                {
                                                    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)8ULL));
                                                    if (!t208) {
                                                        goto gm_lb25;
                                                    }
                                                    void * t209 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t210)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t210, gm_srcj10g653a00lt, 67, 35);
                                                    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)t209) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    void * t212 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t213)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t213, gm_srcj10g653a00lt, 67, 43);
                                                    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)t212) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    uint8_t t215 = (uint8_t)(t211 | t214);
                                                    gm_l31 = t215;
                                                    goto gm_lb23;
                                                }
gm_lb25:;
                                                {
                                                    void * t216 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                    uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t217, gm_srcj10g653a00lt, 68, 30);
                                                    uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)t216) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    void * t219 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t220)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t220, gm_srcj10g653a00lt, 68, 38);
                                                    uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)t219) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    uint8_t t222 = (uint8_t)(t218 ^ t221);
                                                    gm_l31 = t222;
                                                    goto gm_lb23;
                                                }
gm_lb26:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 65, 29);
                                            }
gm_lb23:;
                                            uint8_t t223 = gm_l31;
                                            GM_ST(uint8_t, (((uint8_t *)t196) + (uintptr_t)((uint64_t)0ULL) * 1), t223);
                                        } else {
                                            void * t224 = gm_l0;
                                            gm_tl874t2atnlh3 t225 = gm_l1;
                                            gm_ts6oqppjh0ba3 t226 = gm_l2;
                                            gm_t88rcucg1b7uk t227 = gm_l14;
                                            gm_t88rcucg1b7uk t228 = gm_l17;
                                            gm_t88rcucg1b7uk t229 = gm_l14;
                                            gm_b16_8 t230 = gm_l22;
                                            gm_b16_8 t231 = gm_l23;
                                            gm_b16_8 t232 = gm_l24;
                                            gm_f_binop_nrsq3mp((void *)&gm_l32, (void *)NULL, cx, NULL, t224, t225, t226, t227, t228, t229, t230, t231, t232);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t233 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                            if (t233) {
                                            } else {
                                                gm_t76123gfet73g t234 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l32) + 8));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t234);
                                                goto gm_fdone;
                                            }
                                        }
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 59, 13);
                                }
gm_lb21:;
                            }
gm_lb20:;
                            uint64_t t235 = gm_l20;
                            uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                            uint64_t t238 = r236;
                            gm_l20 = t238;
                        }
                    }
gm_lb18:;
                    goto gm_lb1;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)17ULL));
                    if (!t240) {
                        goto gm_lb27;
                    }
                    void * t241 = gm_l0;
                    gm_t88rcucg1b7uk t242 = gm_l3;
                    gm_tkn42u7na3t81 t243 = gm_f_bits_struct_ty_22ue20s(t241, GM_CONV(gm_csguankko4lmhd, t242));
                    gm_l33 = t243;
                    uint8_t t244 = (uint8_t)((((uint8_t *)&gm_l33))[4] != 0);
                    if (t244) {
                        gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l33));
                        gm_l25 = t245;
                    } else {
                        void * t246 = gm_l0;
                        gm_tl874t2atnlh3 t247 = gm_l1;
                        gm_ts6oqppjh0ba3 t248 = gm_l2;
                        gm_b16_8 t249 = gm_f_op_sym_6olbepb(t248);
                        void * t250 = gm_l0;
                        void * t251 = GM_LD(void *, (((uint8_t *)t250) + 8));
                        gm_t88rcucg1b7uk t252 = gm_l3;
                        gm_b16_8 t253 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t251, t252);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t254;
                        GM_ST(gm_b16_8, ((uint8_t *)&t254 + 0), t249);
                        GM_ST(gm_b16_8, ((uint8_t *)&t254 + 16), t253);
                        gm_f_format_bvot0a7((void *)&gm_l34, (void *)NULL, cx, NULL, &t254);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                        if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcj10g653a00lt, 76, 65);
                        gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                        gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                        gm_t76123gfet73g t258;
                        gm_f_trap_qh84l9d((void *)&t258, (void *)NULL, cx, NULL, t246, t247, t257);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t258);
                        goto gm_fdone;
                    }
                    void * t259 = gm_l0;
                    gm_tl874t2atnlh3 t260 = gm_l1;
                    gm_ts6oqppjh0ba3 t261 = gm_l2;
                    gm_t88rcucg1b7uk t262 = gm_l25;
                    gm_t88rcucg1b7uk t263 = gm_l25;
                    gm_t88rcucg1b7uk t264 = gm_l25;
                    gm_b16_8 t265 = gm_l6;
                    gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                    gm_b16_8 t267 = gm_l7;
                    gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                    gm_b16_8 t269 = gm_l8;
                    gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t259, t260, t261, t262, t263, t264, t266, t268, t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb27:;
                {
                    void * t270 = gm_l0;
                    gm_tl874t2atnlh3 t271 = gm_l1;
                    gm_ts6oqppjh0ba3 t272 = gm_l2;
                    gm_b16_8 t273 = gm_f_op_sym_6olbepb(t272);
                    void * t274 = gm_l0;
                    void * t275 = GM_LD(void *, (((uint8_t *)t274) + 8));
                    gm_t88rcucg1b7uk t276 = gm_l3;
                    gm_b16_8 t277 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t275, t276);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t278;
                    GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t273);
                    GM_ST(gm_b16_8, ((uint8_t *)&t278 + 16), t277);
                    gm_f_format_bvot0a7((void *)&gm_l35, (void *)NULL, cx, NULL, &t278);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t279 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                    if (!t279) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcj10g653a00lt, 79, 35);
                    gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                    gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
                    gm_t76123gfet73g t282;
                    gm_f_trap_qh84l9d((void *)&t282, (void *)NULL, cx, NULL, t270, t271, t281);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t282);
                    goto gm_fdone;
                }
gm_lb28:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 39, 5);
            }
        }
gm_lb1:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bytes_equal_cksjlgj F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t9 = (!(t7 == t8));
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
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
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t17 = gm_l4;
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                        void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t21 = gm_l4;
                        if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srcj10g653a00lt, 347, 24);
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)t21 * 1));
                        uint8_t t23 = (!(t18 == t22));
                        if (t23) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 346, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l3;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l3 = t27;
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
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_cmp_op_mflafbf F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_ttovhfee8mf28 gm_l6; /* info */
    unsigned __int128 gm_l7; /* a */
    unsigned __int128 gm_l8; /* c */
    __int128 gm_l9; /* p */
    __int128 gm_l10; /* q */
    GM_STACK_CHECK(&F, 1166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tg5lql6itufk7 gm_l11;
    gm_t1ec8br9ddkav gm_l12;
    gm_t1ec8br9ddkav gm_l13;
    gm_t1ec8br9ddkav gm_l14;
    gm_t1ec8br9ddkav gm_l15;
    {
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l3;
            gm_tg5lql6itufk7 t21 = gm_f_int_info_ulcevdv(t19, GM_CONV(gm_csguankko4lmhd, t20));
            gm_l11 = t21;
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
            if (!t22) {
                goto gm_lb2;
            }
            gm_ttovhfee8mf28 t23 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l11));
            gm_l6 = t23;
            void * t24 = gm_l0;
            gm_ttovhfee8mf28 t25 = gm_l6;
            gm_b16_8 t26 = gm_l4;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            unsigned __int128 t28 = gm_f_rd_int_iuioodt(t24, t25, t27);
            gm_l7 = t28;
            void * t29 = gm_l0;
            gm_ttovhfee8mf28 t30 = gm_l6;
            gm_b16_8 t31 = gm_l5;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            unsigned __int128 t33 = gm_f_rd_int_iuioodt(t29, t30, t32);
            gm_l8 = t33;
            uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
            if (t34) {
                unsigned __int128 t35 = gm_l7;
                __int128 t36;
                memset(&t36, 0, sizeof t36); memcpy(&t36, ((uint8_t *)&t35), sizeof t36 < 16 ? sizeof t36 : 16);
                gm_l9 = t36;
                unsigned __int128 t37 = gm_l8;
                __int128 t38;
                memset(&t38, 0, sizeof t38); memcpy(&t38, ((uint8_t *)&t37), sizeof t38 < 16 ? sizeof t38 : 16);
                gm_l10 = t38;
                {
                    {
                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t40 = (uint8_t)(t39 == ((uint8_t)10ULL));
                        if (!t40) {
                            goto gm_lb4;
                        }
                        __int128 t41 = gm_l9;
                        __int128 t42 = gm_l10;
                        uint8_t t43 = (t41 < t42);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 24, 0);
                        ((uint8_t *)&gm_l12)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l12 + 8), t43);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t45 = (uint8_t)(t44 == ((uint8_t)11ULL));
                        if (!t45) {
                            goto gm_lb5;
                        }
                        __int128 t46 = gm_l9;
                        __int128 t47 = gm_l10;
                        uint8_t t48 = (t46 <= t47);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 24, 0);
                        ((uint8_t *)&gm_l12)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l12 + 8), t48);
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t50 = (uint8_t)(t49 == ((uint8_t)12ULL));
                        if (!t50) {
                            goto gm_lb6;
                        }
                        __int128 t51 = gm_l9;
                        __int128 t52 = gm_l10;
                        uint8_t t53 = (t51 > t52);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 24, 0);
                        ((uint8_t *)&gm_l12)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l12 + 8), t53);
                        goto gm_lb3;
                    }
gm_lb6:;
                    {
                        __int128 t54 = gm_l9;
                        __int128 t55 = gm_l10;
                        uint8_t t56 = (t54 >= t55);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l12 + 24, 0);
                        ((uint8_t *)&gm_l12)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l12 + 8), t56);
                        goto gm_lb3;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 148, 20);
                }
gm_lb3:;
                memmove(gm_out, ((uint8_t *)&gm_l12), 32);
                goto gm_fdone;
            }
            {
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)10ULL));
                    if (!t58) {
                        goto gm_lb9;
                    }
                    unsigned __int128 t59 = gm_l7;
                    unsigned __int128 t60 = gm_l8;
                    uint8_t t61 = (t59 < t60);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 24, 0);
                    ((uint8_t *)&gm_l13)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&gm_l13 + 8), t61);
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)11ULL));
                    if (!t63) {
                        goto gm_lb10;
                    }
                    unsigned __int128 t64 = gm_l7;
                    unsigned __int128 t65 = gm_l8;
                    uint8_t t66 = (t64 <= t65);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 24, 0);
                    ((uint8_t *)&gm_l13)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&gm_l13 + 8), t66);
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)12ULL));
                    if (!t68) {
                        goto gm_lb11;
                    }
                    unsigned __int128 t69 = gm_l7;
                    unsigned __int128 t70 = gm_l8;
                    uint8_t t71 = (t69 > t70);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 24, 0);
                    ((uint8_t *)&gm_l13)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&gm_l13 + 8), t71);
                    goto gm_lb8;
                }
gm_lb11:;
                {
                    unsigned __int128 t72 = gm_l7;
                    unsigned __int128 t73 = gm_l8;
                    uint8_t t74 = (t72 >= t73);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&gm_l13 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l13 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l13 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&gm_l13 + 24, 0);
                    ((uint8_t *)&gm_l13)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&gm_l13 + 8), t74);
                    goto gm_lb8;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 155, 16);
            }
gm_lb8:;
            memmove(gm_out, ((uint8_t *)&gm_l13), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)10ULL));
            if (!t76) {
                goto gm_lb14;
            }
            void * t77 = gm_l0;
            gm_tl874t2atnlh3 t78 = gm_l1;
            gm_t88rcucg1b7uk t79 = gm_l3;
            gm_b16_8 t80 = gm_l4;
            gm_b16_8 t81 = gm_l5;
            gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t77, t78, t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb14:;
        {
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)12ULL));
            if (!t83) {
                goto gm_lb15;
            }
            void * t84 = gm_l0;
            gm_tl874t2atnlh3 t85 = gm_l1;
            gm_t88rcucg1b7uk t86 = gm_l3;
            gm_b16_8 t87 = gm_l5;
            gm_b16_8 t88 = gm_l4;
            gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t84, t85, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb15:;
        {
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)11ULL));
            if (!t90) {
                goto gm_lb16;
            }
            {
                void * t91 = gm_l0;
                gm_tl874t2atnlh3 t92 = gm_l1;
                gm_t88rcucg1b7uk t93 = gm_l3;
                gm_b16_8 t94 = gm_l4;
                gm_b16_8 t95 = gm_l5;
                gm_f_less_nbfe87l((void *)&gm_l14, (void *)NULL, cx, NULL, t91, t92, t93, t94, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (t96) {
            } else {
                gm_t76123gfet73g t97 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l14) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t97);
                goto gm_fdone;
            }
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 8));
            if (t98) {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                goto gm_fdone;
            }
            {
                void * t99 = gm_l0;
                gm_tl874t2atnlh3 t100 = gm_l1;
                gm_t88rcucg1b7uk t101 = gm_l3;
                gm_b16_8 t102 = gm_l4;
                gm_b16_8 t103 = gm_l5;
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t99, t100, t101, t102, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
        }
gm_lb16:;
        {
            {
                void * t104 = gm_l0;
                gm_tl874t2atnlh3 t105 = gm_l1;
                gm_t88rcucg1b7uk t106 = gm_l3;
                gm_b16_8 t107 = gm_l5;
                gm_b16_8 t108 = gm_l4;
                gm_f_less_nbfe87l((void *)&gm_l15, (void *)NULL, cx, NULL, t104, t105, t106, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (t109) {
            } else {
                gm_t76123gfet73g t110 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l15) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t110);
                goto gm_fdone;
            }
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 8));
            if (t111) {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                goto gm_fdone;
            }
            {
                void * t112 = gm_l0;
                gm_tl874t2atnlh3 t113 = gm_l1;
                gm_t88rcucg1b7uk t114 = gm_l3;
                gm_b16_8 t115 = gm_l5;
                gm_b16_8 t116 = gm_l4;
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t112, t113, t114, t115, t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 162, 5);
    }
gm_lb13:;
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
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_contains_nugmipn F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* _elem_ty */
    gm_b16_8 gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* cty */
    gm_b16_8 gm_l5; /* c */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* elems */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* a */
    gm_b16_8 gm_l15; /* other */
    uint8_t gm_l16; /* incl */
    gm_tfbnciphf3261 gm_l17; /* lf */
    gm_tfbnciphf3261 gm_l18; /* hf */
    gm_b16_8 gm_l19; /* lo */
    gm_b16_8 gm_l20; /* hi */
    uint8_t gm_l21; /* ge_lo */
    uint8_t gm_l22; /* below */
    GM_STACK_CHECK(&F, 2057u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l23;
    gm_tmc91gv7n9c0i gm_l24;
    gm_t1ec8br9ddkav gm_l25;
    gm_t1ec8br9ddkav gm_l26;
    void * gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_t1ec8br9ddkav gm_l29;
    gm_t1ec8br9ddkav gm_l30;
    gm_t1ec8br9ddkav gm_l31;
    uint8_t gm_l32;
    gm_tabb710d9mq2e gm_l33;
    {
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l4;
        gm_f_kind_htv6093((void *)&gm_l23, t24, GM_CONV(gm_csguankko4lmhd, t25));
        {
            {
                {
                    {
                        {
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)11ULL));
                            if (!t27) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)10ULL));
                            if (!t29) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        void * t30 = gm_l0;
                        gm_tl874t2atnlh3 t31 = gm_l1;
                        gm_t88rcucg1b7uk t32 = gm_l4;
                        gm_b16_8 t33 = gm_l5;
                        gm_f_seq_of_ifahtjs((void *)&gm_l24, (void *)NULL, cx, NULL, t30, t31, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (t34) {
                    } else {
                        gm_t76123gfet73g t35 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l24) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t35);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l24) + 8), 40);
                    }
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
                    if (t37) {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                    gm_l8 = ((uint64_t)0ULL);
                    uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)&F.l6) + 24) + 8));
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    if (t39 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 363, 25);
                    uint64_t t40 = (uint64_t)(t38 / t39);
                    gm_l7 = t40;
                    {
                        for (;;) {
                            uint64_t t41 = gm_l8;
                            uint64_t t42 = gm_l7;
                            uint8_t t43 = (t41 < t42);
                            if (!t43) {
                                goto gm_lb6;
                            }
                            {
                                {
                                    {
                                        uint64_t t44 = gm_l8;
                                        gm_l9 = t44;
                                        void * t45 = gm_l0;
                                        gm_tl874t2atnlh3 t46 = gm_l1;
                                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l6));
                                        void * t48 = GM_LD(void *, (((uint8_t *)&F.l6) + 24));
                                        uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)&F.l6) + 24) + 8));
                                        uint64_t t50 = gm_l9;
                                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                        uint64_t r52; bool o53 = __builtin_mul_overflow(t50, t51, &r52);
                                        if (o53) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 364, 53);
                                        uint64_t t54 = r52;
                                        uint64_t t55 = gm_l9;
                                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                        if (o57) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 364, 65);
                                        uint64_t t58 = r56;
                                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                        uint64_t r60; bool o61 = __builtin_mul_overflow(t58, t59, &r60);
                                        if (o61) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 364, 64);
                                        uint64_t t62 = r60;
                                        uint64_t t63 = (uintptr_t)t54;
                                        uint64_t t64 = (uintptr_t)t62;
                                        if (t63 > t64 || t64 > (uintptr_t)t49) gm_trap_slice(t63, t64, t49, gm_srcj10g653a00lt, 364, 46);
                                        uint64_t t65 = ((uintptr_t)t62) - (uintptr_t)t54;
                                        gm_b16_8 t66;
                                        GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t48 + (uintptr_t)t54 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(t65));
                                        gm_b16_8 t67 = gm_l3;
                                        gm_f_equal_e5vf82h((void *)&gm_l25, (void *)NULL, cx, NULL, t45, t46, t47, t66, t67);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                        if (t68) {
                                        } else {
                                            gm_t76123gfet73g t69 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l25) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t69);
                                            goto gm_fdone;
                                        }
                                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
                                        if (t70) {
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                                            goto gm_fdone;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 363, 13);
                                }
gm_lb9:;
                            }
gm_lb8:;
                            uint64_t t71 = gm_l8;
                            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                            uint64_t t74 = r72;
                            gm_l8 = t74;
                        }
                    }
gm_lb6:;
                    {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)12ULL));
                    if (!t76) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                    gm_l10 = t77;
                    gm_l12 = ((uint64_t)0ULL);
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    gm_l11 = t78;
                    {
                        for (;;) {
                            uint64_t t79 = gm_l12;
                            uint64_t t80 = gm_l11;
                            uint8_t t81 = (t79 < t80);
                            if (!t81) {
                                goto gm_lb12;
                            }
                            {
                                {
                                    {
                                        uint64_t t82 = gm_l12;
                                        gm_l13 = t82;
                                        void * t83 = gm_l0;
                                        gm_tl874t2atnlh3 t84 = gm_l1;
                                        void * t85 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint64_t t87 = gm_l13;
                                        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + (uintptr_t)t87 * 4));
                                        void * t89 = gm_l0;
                                        gm_t88rcucg1b7uk t90 = gm_l4;
                                        gm_b16_8 t91 = gm_l5;
                                        uint64_t t92 = gm_l13;
                                        uint32_t t93 = (uint32_t)t92;
                                        gm_b16_8 t94 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t89, t90, t91, t93);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t95 = gm_l3;
                                        gm_f_equal_e5vf82h((void *)&gm_l26, (void *)NULL, cx, NULL, t83, t84, t88, t94, t95);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                        if (t96) {
                                        } else {
                                            gm_t76123gfet73g t97 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l26) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t97);
                                            goto gm_fdone;
                                        }
                                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
                                        if (t98) {
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                                            goto gm_fdone;
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 369, 13);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t99 = gm_l12;
                            uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                            uint64_t t102 = r100;
                            gm_l12 = t102;
                        }
                    }
gm_lb12:;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)17ULL));
                    if (!t104) {
                        goto gm_lb17;
                    }
                    void * t105 = gm_l0;
                    gm_t88rcucg1b7uk t106 = gm_l4;
                    void * t107 = gm_f_adt_of_8k5vqud(t105, GM_CONV(gm_csguankko4lmhd, t106));
                    gm_l27 = t107;
                    uint8_t t108 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l27)) != 0);
                    if (t108) {
                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l27));
                        gm_l14 = t109;
                    } else {
                        void * t110 = gm_l0;
                        gm_tl874t2atnlh3 t111 = gm_l1;
                        gm_b16_8 t112;
                        GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sa2pnk1jujp5p));
                        GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(22));
                        gm_t76123gfet73g t113;
                        gm_f_trap_qh84l9d((void *)&t113, (void *)NULL, cx, NULL, t110, t111, t112);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t113);
                        goto gm_fdone;
                    }
                    void * t114 = gm_l14;
                    {
                        {
                            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)t114));
                            gm_b16_8 t116;
                            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_seue7mq07v0oc));
                            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(5));
                            uint8_t t117 = (gm_eq0iqiubfm7e(((uint8_t *)&t115), ((uint8_t *)&t116)));
                            if (!t117) {
                                goto gm_lb19;
                            }
                            gm_l16 = ((uint8_t)0);
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)t114));
                            gm_b16_8 t119;
                            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s43cq4krigs2b));
                            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(9));
                            uint8_t t120 = (gm_eq0iqiubfm7e(((uint8_t *)&t118), ((uint8_t *)&t119)));
                            if (!t120) {
                                goto gm_lb20;
                            }
                            gm_l16 = ((uint8_t)1);
                            goto gm_lb18;
                        }
gm_lb20:;
                        {
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)t114));
                            gm_l15 = t121;
                            {
                                void * t122 = gm_l0;
                                gm_tl874t2atnlh3 t123 = gm_l1;
                                gm_b16_8 t124 = gm_l15;
                                gm_tnpifv20irnph t125;
                                GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
                                gm_f_format_kfcf8tk((void *)&gm_l28, (void *)NULL, cx, NULL, t125);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcj10g653a00lt, 379, 47);
                                gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                gm_t76123gfet73g t129;
                                gm_f_trap_qh84l9d((void *)&t129, (void *)NULL, cx, NULL, t122, t123, t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t129);
                                goto gm_fdone;
                            }
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 376, 21);
                    }
gm_lb18:;
                    void * t130 = gm_l0;
                    gm_t88rcucg1b7uk t131 = gm_l4;
                    gm_tfbnciphf3261 t132 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t130, t131, ((uint32_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l17 = t132;
                    void * t133 = gm_l0;
                    gm_t88rcucg1b7uk t134 = gm_l4;
                    gm_tfbnciphf3261 t135 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t133, t134, ((uint32_t)1ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t135;
                    void * t136 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t139 = gm_f_us_cuv01fp(t138);
                    uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t141 = gm_f_us_cuv01fp(t140);
                    void * t142 = gm_l0;
                    gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                    uint64_t t144 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t142, t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t145 = gm_f_us_cuv01fp(t144);
                    uint64_t r146; bool o147 = __builtin_add_overflow(t141, t145, &r146);
                    if (o147) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 383, 33);
                    uint64_t t148 = r146;
                    uint64_t t149 = (uintptr_t)t139;
                    uint64_t t150 = (uintptr_t)t148;
                    if (t149 > t150 || t150 > (uintptr_t)t137) gm_trap_slice(t149, t150, t137, gm_srcj10g653a00lt, 383, 19);
                    uint64_t t151 = ((uintptr_t)t148) - (uintptr_t)t139;
                    gm_b16_8 t152;
                    GM_ST(void *, (uint8_t *)&t152, (void *)(((uint8_t *)t136 + (uintptr_t)t139 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(t151));
                    gm_l19 = t152;
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t156 = gm_f_us_cuv01fp(t155);
                    uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t158 = gm_f_us_cuv01fp(t157);
                    void * t159 = gm_l0;
                    gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l18));
                    uint64_t t161 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t159, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t162 = gm_f_us_cuv01fp(t161);
                    uint64_t r163; bool o164 = __builtin_add_overflow(t158, t162, &r163);
                    if (o164) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 384, 33);
                    uint64_t t165 = r163;
                    uint64_t t166 = (uintptr_t)t156;
                    uint64_t t167 = (uintptr_t)t165;
                    if (t166 > t167 || t167 > (uintptr_t)t154) gm_trap_slice(t166, t167, t154, gm_srcj10g653a00lt, 384, 19);
                    uint64_t t168 = ((uintptr_t)t165) - (uintptr_t)t156;
                    gm_b16_8 t169;
                    GM_ST(void *, (uint8_t *)&t169, (void *)(((uint8_t *)t153 + (uintptr_t)t156 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(t168));
                    gm_l20 = t169;
                    {
                        void * t170 = gm_l0;
                        gm_tl874t2atnlh3 t171 = gm_l1;
                        gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                        gm_b16_8 t173 = gm_l3;
                        gm_b16_8 t174 = gm_l19;
                        gm_f_less_nbfe87l((void *)&gm_l29, (void *)NULL, cx, NULL, t170, t171, t172, t173, t174);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (t175) {
                    } else {
                        gm_t76123gfet73g t176 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l29) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t176);
                        goto gm_fdone;
                    }
                    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 8));
                    uint8_t t178 = (uint8_t)!t177;
                    gm_l21 = t178;
                    uint8_t t179 = gm_l16;
                    if (t179) {
                        void * t180 = gm_l0;
                        gm_tl874t2atnlh3 t181 = gm_l1;
                        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                        gm_b16_8 t183 = gm_l20;
                        gm_b16_8 t184 = gm_l3;
                        gm_f_less_nbfe87l((void *)&gm_l30, (void *)NULL, cx, NULL, t180, t181, t182, t183, t184);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                        if (t185) {
                        } else {
                            gm_t76123gfet73g t186 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l30) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t186);
                            goto gm_fdone;
                        }
                        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 8));
                        uint8_t t188 = (uint8_t)!t187;
                        gm_l22 = t188;
                    } else {
                        void * t189 = gm_l0;
                        gm_tl874t2atnlh3 t190 = gm_l1;
                        gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                        gm_b16_8 t192 = gm_l3;
                        gm_b16_8 t193 = gm_l20;
                        gm_f_less_nbfe87l((void *)&gm_l31, (void *)NULL, cx, NULL, t189, t190, t191, t192, t193);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (t194) {
                        } else {
                            gm_t76123gfet73g t195 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l31) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t195);
                            goto gm_fdone;
                        }
                        uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 8));
                        gm_l22 = t196;
                    }
                    uint8_t t197 = gm_l21;
                    if (t197) {
                        uint8_t t198 = gm_l22;
                        gm_l32 = t198;
                    } else {
                        gm_l32 = ((uint8_t)0);
                    }
                    {
                        uint8_t t199 = gm_l32;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t199);
                        goto gm_fdone;
                    }
                }
gm_lb17:;
            }
            {
                {
                    void * t200 = gm_l0;
                    gm_tl874t2atnlh3 t201 = gm_l1;
                    void * t202 = gm_l0;
                    void * t203 = GM_LD(void *, (((uint8_t *)t202) + 8));
                    gm_t88rcucg1b7uk t204 = gm_l4;
                    gm_b16_8 t205 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t203, t204);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t206;
                    GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                    gm_f_format_kfcf8tk((void *)&gm_l33, (void *)NULL, cx, NULL, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcj10g653a00lt, 389, 35);
                    gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                    gm_t76123gfet73g t210;
                    gm_f_trap_qh84l9d((void *)&t210, (void *)NULL, cx, NULL, t200, t201, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t210);
                    goto gm_fdone;
                }
gm_lb22:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 359, 5);
            }
        }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_equal_e5vf82h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* elems */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    uint64_t gm_l12; /* esz */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_t2lqu8jsj511d gm_l17; /* pa */
    gm_t2lqu8jsj511d gm_l18; /* pb */
    uint8_t gm_l19; /* is_float */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* inner */
    uint8_t gm_l26; /* pa */
    uint8_t gm_l27; /* pb */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    uint8_t gm_l30; /* ea */
    gm_t88rcucg1b7uk gm_l31; /* u */
    gm_tcmkompbl09il gm_l32; /* u */
    uint32_t gm_l33; /* va */
    uint32_t gm_l34; /* vb */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    uint64_t gm_l36; /* off */
    uint64_t gm_l37; /* n */
    void * gm_l38; /* a */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tfbnciphf3261 gm_l43; /* fl */
    uint64_t gm_l44; /* off */
    uint64_t gm_l45; /* n */
    GM_STACK_CHECK(&F, 4727u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l46;
    gm_tg5lql6itufk7 gm_l47;
    gm_t1ec8br9ddkav gm_l48;
    gm_t1ec8br9ddkav gm_l49;
    gm_t53at80pfdus3 gm_l50;
    uint8_t gm_l51;
    uint8_t gm_l52;
    gm_tmc91gv7n9c0i gm_l53;
    gm_tmc91gv7n9c0i gm_l54;
    uint8_t gm_l55;
    gm_t53at80pfdus3 gm_l56;
    uint8_t gm_l57;
    gm_t1ec8br9ddkav gm_l58;
    gm_t88rcucg1b7uk gm_l59;
    gm_tkn42u7na3t81 gm_l60;
    gm_t57euh612ligr gm_l61;
    gm_t9805qtonbsag gm_l62;
    gm_t9805qtonbsag gm_l63;
    void * gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_t1ec8br9ddkav gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    {
        void * t64 = gm_l0;
        gm_t88rcucg1b7uk t65 = gm_l2;
        gm_f_kind_htv6093((void *)&gm_l46, t64, GM_CONV(gm_csguankko4lmhd, t65));
        {
            {
                {
                    {
                        {
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)0ULL));
                            if (!t67) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)1ULL));
                            if (!t69) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)2ULL));
                            if (!t71) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t72 = gm_l0;
                    gm_t88rcucg1b7uk t73 = gm_l2;
                    gm_tg5lql6itufk7 t74 = gm_f_int_info_ulcevdv(t72, GM_CONV(gm_csguankko4lmhd, t73));
                    gm_l47 = t74;
                    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l47))[8] != 0);
                    if (t75) {
                        gm_ttovhfee8mf28 t76 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l47));
                        gm_l5 = t76;
                    } else {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                    void * t77 = gm_l0;
                    gm_ttovhfee8mf28 t78 = gm_l5;
                    gm_b16_8 t79 = gm_l3;
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                    unsigned __int128 t81 = gm_f_rd_int_iuioodt(t77, t78, t80);
                    void * t82 = gm_l0;
                    gm_ttovhfee8mf28 t83 = gm_l5;
                    gm_b16_8 t84 = gm_l4;
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                    unsigned __int128 t86 = gm_f_rd_int_iuioodt(t82, t83, t85);
                    uint8_t t87 = ((t81 == t86));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t87);
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)3ULL));
                    if (!t89) {
                        goto gm_lb7;
                    }
                    void * t90 = gm_l0;
                    gm_t88rcucg1b7uk t91 = gm_l2;
                    gm_b16_8 t92 = gm_l3;
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                    double t94 = gm_f_get_float_grmhlm4(t90, GM_CONV(gm_csguankko4lmhd, t91), t93);
                    void * t95 = gm_l0;
                    gm_t88rcucg1b7uk t96 = gm_l2;
                    gm_b16_8 t97 = gm_l4;
                    gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                    double t99 = gm_f_get_float_grmhlm4(t95, GM_CONV(gm_csguankko4lmhd, t96), t98);
                    uint8_t t100 = ((t94 == t99));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t100);
                    goto gm_fdone;
                }
gm_lb7:;
                {
                    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t102 = (uint8_t)(t101 == ((uint8_t)4ULL));
                    if (!t102) {
                        goto gm_lb8;
                    }
                    void * t103 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t104)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t104, gm_srcj10g653a00lt, 259, 26);
                    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t103) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t106 = (!(t105 == ((uint8_t)0ULL)));
                    void * t107 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t108, gm_srcj10g653a00lt, 259, 45);
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)t107) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t110 = (!(t109 == ((uint8_t)0ULL)));
                    uint8_t t111 = ((t106 == t110));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t111);
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)8ULL));
                            if (!t113) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)9ULL));
                            if (!t115) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        {
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)7ULL));
                            if (!t117) {
                                goto gm_lb13;
                            }
                            goto gm_lb10;
                        }
gm_lb13:;
                        {
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t119 = (uint8_t)(t118 == ((uint8_t)16ULL));
                            if (!t119) {
                                goto gm_lb14;
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t120 = gm_l0;
                    gm_b16_8 t121 = gm_l3;
                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                    uint64_t t123 = gm_f_ptr_of_b77b4c9(t120, t122);
                    void * t124 = gm_l0;
                    gm_b16_8 t125 = gm_l4;
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                    uint64_t t127 = gm_f_ptr_of_b77b4c9(t124, t126);
                    uint8_t t128 = ((t123 == t127));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t128);
                    goto gm_fdone;
                }
gm_lb9:;
                {
                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)5ULL));
                    if (!t130) {
                        goto gm_lb15;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                    goto gm_fdone;
                }
gm_lb15:;
                {
                    uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t132 = (uint8_t)(t131 == ((uint8_t)12ULL));
                    if (!t132) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                    gm_l6 = t133;
                    gm_l8 = ((uint64_t)0ULL);
                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l7 = t134;
                    {
                        for (;;) {
                            uint64_t t135 = gm_l8;
                            uint64_t t136 = gm_l7;
                            uint8_t t137 = (t135 < t136);
                            if (!t137) {
                                goto gm_lb17;
                            }
                            {
                                {
                                    {
                                        uint64_t t138 = gm_l8;
                                        gm_l9 = t138;
                                        void * t139 = gm_l0;
                                        gm_tl874t2atnlh3 t140 = gm_l1;
                                        void * t141 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t143 = gm_l9;
                                        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + (uintptr_t)t143 * 4));
                                        void * t145 = gm_l0;
                                        gm_t88rcucg1b7uk t146 = gm_l2;
                                        gm_b16_8 t147 = gm_l3;
                                        uint64_t t148 = gm_l9;
                                        uint32_t t149 = (uint32_t)t148;
                                        gm_b16_8 t150 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t145, t146, t147, t149);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t151 = gm_l0;
                                        gm_t88rcucg1b7uk t152 = gm_l2;
                                        gm_b16_8 t153 = gm_l4;
                                        uint64_t t154 = gm_l9;
                                        uint32_t t155 = (uint32_t)t154;
                                        gm_b16_8 t156 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t151, t152, t153, t155);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_f_equal_e5vf82h((void *)&gm_l48, (void *)NULL, cx, NULL, t139, t140, t144, t150, t156);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                        if (t157) {
                                        } else {
                                            gm_t76123gfet73g t158 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l48) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t158);
                                            goto gm_fdone;
                                        }
                                        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 8));
                                        uint8_t t160 = (uint8_t)!t159;
                                        if (t160) {
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                            goto gm_fdone;
                                        }
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 263, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t161 = gm_l8;
                            uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                            uint64_t t164 = r162;
                            gm_l8 = t164;
                        }
                    }
gm_lb17:;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                    goto gm_fdone;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)11ULL));
                    if (!t166) {
                        goto gm_lb22;
                    }
                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                    gm_l10 = t167;
                    gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l46) + 8) + 8));
                    gm_l11 = t168;
                    void * t169 = gm_l0;
                    gm_t88rcucg1b7uk t170 = gm_l11;
                    uint64_t t171 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t169, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t172 = gm_f_us_cuv01fp(t171);
                    gm_l12 = t172;
                    gm_l14 = ((uint64_t)0ULL);
                    uint64_t t173 = gm_l10;
                    uint64_t t174 = gm_f_us_cuv01fp(t173);
                    gm_l13 = t174;
                    {
                        for (;;) {
                            uint64_t t175 = gm_l14;
                            uint64_t t176 = gm_l13;
                            uint8_t t177 = (t175 < t176);
                            if (!t177) {
                                goto gm_lb23;
                            }
                            {
                                {
                                    {
                                        uint64_t t178 = gm_l14;
                                        gm_l15 = t178;
                                        void * t179 = gm_l0;
                                        gm_tl874t2atnlh3 t180 = gm_l1;
                                        gm_t88rcucg1b7uk t181 = gm_l11;
                                        void * t182 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t184 = gm_l15;
                                        uint64_t t185 = gm_l12;
                                        uint64_t r186; bool o187 = __builtin_mul_overflow(t184, t185, &r186);
                                        if (o187) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 271, 50);
                                        uint64_t t188 = r186;
                                        uint64_t t189 = gm_l15;
                                        uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                        if (o191) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 271, 60);
                                        uint64_t t192 = r190;
                                        uint64_t t193 = gm_l12;
                                        uint64_t r194; bool o195 = __builtin_mul_overflow(t192, t193, &r194);
                                        if (o195) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 271, 59);
                                        uint64_t t196 = r194;
                                        uint64_t t197 = (uintptr_t)t188;
                                        uint64_t t198 = (uintptr_t)t196;
                                        if (t197 > t198 || t198 > (uintptr_t)t183) gm_trap_slice(t197, t198, t183, gm_srcj10g653a00lt, 271, 48);
                                        uint64_t t199 = ((uintptr_t)t196) - (uintptr_t)t188;
                                        gm_b16_8 t200;
                                        GM_ST(void *, (uint8_t *)&t200, (void *)(((uint8_t *)t182 + (uintptr_t)t188 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(t199));
                                        void * t201 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                        uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                        uint64_t t203 = gm_l15;
                                        uint64_t t204 = gm_l12;
                                        uint64_t r205; bool o206 = __builtin_mul_overflow(t203, t204, &r205);
                                        if (o206) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 271, 77);
                                        uint64_t t207 = r205;
                                        uint64_t t208 = gm_l15;
                                        uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                                        if (o210) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 271, 87);
                                        uint64_t t211 = r209;
                                        uint64_t t212 = gm_l12;
                                        uint64_t r213; bool o214 = __builtin_mul_overflow(t211, t212, &r213);
                                        if (o214) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 271, 86);
                                        uint64_t t215 = r213;
                                        uint64_t t216 = (uintptr_t)t207;
                                        uint64_t t217 = (uintptr_t)t215;
                                        if (t216 > t217 || t217 > (uintptr_t)t202) gm_trap_slice(t216, t217, t202, gm_srcj10g653a00lt, 271, 75);
                                        uint64_t t218 = ((uintptr_t)t215) - (uintptr_t)t207;
                                        gm_b16_8 t219;
                                        GM_ST(void *, (uint8_t *)&t219, (void *)(((uint8_t *)t201 + (uintptr_t)t207 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(t218));
                                        gm_f_equal_e5vf82h((void *)&gm_l49, (void *)NULL, cx, NULL, t179, t180, t181, t200, t219);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                        if (t220) {
                                        } else {
                                            gm_t76123gfet73g t221 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l49) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t221);
                                            goto gm_fdone;
                                        }
                                        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 8));
                                        uint8_t t223 = (uint8_t)!t222;
                                        if (t223) {
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                            goto gm_fdone;
                                        }
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 270, 13);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t224 = gm_l14;
                            uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                            uint64_t t227 = r225;
                            gm_l14 = t227;
                        }
                    }
gm_lb23:;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                    goto gm_fdone;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t229 = (uint8_t)(t228 == ((uint8_t)10ULL));
                    if (!t229) {
                        goto gm_lb28;
                    }
                    gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l46) + 8) + 4));
                    gm_l16 = t230;
                    void * t231 = gm_l0;
                    gm_b16_8 t232 = gm_l3;
                    gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                    gm_t2lqu8jsj511d t234 = gm_f_slice_parts_1se5hu4(t231, t233);
                    gm_l17 = t234;
                    void * t235 = gm_l0;
                    gm_b16_8 t236 = gm_l4;
                    gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                    gm_t2lqu8jsj511d t238 = gm_f_slice_parts_1se5hu4(t235, t237);
                    gm_l18 = t238;
                    uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t241 = (!(t239 == t240));
                    if (t241) {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                    {
                        void * t242 = gm_l0;
                        gm_t88rcucg1b7uk t243 = gm_l16;
                        gm_f_kind_htv6093((void *)&gm_l50, t242, GM_CONV(gm_csguankko4lmhd, t243));
                    }
                    {
                        {
                            uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t245 = (uint8_t)(t244 == ((uint8_t)3ULL));
                            if (!t245) {
                                goto gm_lb30;
                            }
                            gm_l19 = ((uint8_t)1);
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            gm_l19 = ((uint8_t)0);
                            goto gm_lb29;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 279, 25);
                    }
gm_lb29:;
                    {
                        {
                            uint64_t t246 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                            uint64_t t247 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                            uint8_t t248 = ((t246 == t247));
                            if (t248) {
                                gm_l51 = ((uint8_t)1);
                            } else {
                                uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint8_t t250 = ((t249 == ((uint64_t)0ULL)));
                                gm_l51 = t250;
                            }
                            uint8_t t251 = gm_l51;
                            if (!t251) {
                                goto gm_lb33;
                            }
                            uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t253 = ((t252 == ((uint64_t)0ULL)));
                            if (t253) {
                                gm_l52 = ((uint8_t)1);
                            } else {
                                uint8_t t254 = gm_l19;
                                uint8_t t255 = (uint8_t)!t254;
                                gm_l52 = t255;
                            }
                            uint8_t t256 = gm_l52;
                            if (!t256) {
                                goto gm_lb33;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                            goto gm_fdone;
                        }
gm_lb33:;
                    }
gm_lb32:;
                    {
                        void * t257 = gm_l0;
                        gm_tl874t2atnlh3 t258 = gm_l1;
                        gm_t88rcucg1b7uk t259 = gm_l2;
                        gm_b16_8 t260 = gm_l3;
                        gm_f_seq_of_ifahtjs((void *)&gm_l53, (void *)NULL, cx, NULL, t257, t258, t259, t260);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (t261) {
                    } else {
                        gm_t76123gfet73g t262 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l53) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t262);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l20), (((uint8_t *)&gm_l53) + 8), 40);
                    }
                    {
                        void * t263 = gm_l0;
                        gm_tl874t2atnlh3 t264 = gm_l1;
                        gm_t88rcucg1b7uk t265 = gm_l2;
                        gm_b16_8 t266 = gm_l4;
                        gm_f_seq_of_ifahtjs((void *)&gm_l54, (void *)NULL, cx, NULL, t263, t264, t265, t266);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t267 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (t267) {
                    } else {
                        gm_t76123gfet73g t268 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l54) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t268);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l21), (((uint8_t *)&gm_l54) + 8), 40);
                    }
                    {
                        {
                            uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint8_t t270 = ((t269 == ((uint64_t)1ULL)));
                            if (!t270) {
                                goto gm_lb35;
                            }
                            {
                                void * t271 = gm_l0;
                                gm_t88rcucg1b7uk t272 = gm_l16;
                                gm_f_kind_htv6093((void *)&gm_l56, t271, GM_CONV(gm_csguankko4lmhd, t272));
                            }
                            {
                                {
                                    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l56) + 0));
                                    uint8_t t274 = (uint8_t)(t273 == ((uint8_t)0ULL));
                                    if (!t274) {
                                        goto gm_lb37;
                                    }
                                    gm_l55 = ((uint8_t)1);
                                    goto gm_lb36;
                                }
gm_lb37:;
                                {
                                    gm_l55 = ((uint8_t)0);
                                    goto gm_lb36;
                                }
gm_lb38:;
                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 284, 32);
                            }
gm_lb36:;
                            uint8_t t275 = gm_l55;
                            if (!t275) {
                                goto gm_lb35;
                            }
                            uint64_t t276 = GM_LD(uint64_t, ((((uint8_t *)&F.l20) + 24) + 8));
                            uint64_t t277 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 24) + 8));
                            uint8_t t278 = ((t276 == t277));
                            if (t278) {
                                gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 24));
                                gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                                uint8_t t283 = gm_f_bytes_equal_cksjlgj(t280, t282);
                                gm_l57 = t283;
                            } else {
                                gm_l57 = ((uint8_t)0);
                            }
                            {
                                uint8_t t284 = gm_l57;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t284);
                                goto gm_fdone;
                            }
                        }
gm_lb35:;
                    }
gm_lb34:;
                    gm_l23 = ((uint64_t)0ULL);
                    uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t286 = gm_f_us_cuv01fp(t285);
                    gm_l22 = t286;
                    {
                        for (;;) {
                            uint64_t t287 = gm_l23;
                            uint64_t t288 = gm_l22;
                            uint8_t t289 = (t287 < t288);
                            if (!t289) {
                                goto gm_lb39;
                            }
                            {
                                {
                                    {
                                        uint64_t t290 = gm_l23;
                                        gm_l24 = t290;
                                        void * t291 = gm_l0;
                                        gm_tl874t2atnlh3 t292 = gm_l1;
                                        gm_t88rcucg1b7uk t293 = gm_l16;
                                        void * t294 = GM_LD(void *, (((uint8_t *)&F.l20) + 24));
                                        uint64_t t295 = GM_LD(uint64_t, ((((uint8_t *)&F.l20) + 24) + 8));
                                        uint64_t t296 = gm_l24;
                                        uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                                        uint64_t r298; bool o299 = __builtin_mul_overflow(t296, t297, &r298);
                                        if (o299) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 286, 56);
                                        uint64_t t300 = r298;
                                        uint64_t t301 = gm_l24;
                                        uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                                        if (o303) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 286, 69);
                                        uint64_t t304 = r302;
                                        uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                                        uint64_t r306; bool o307 = __builtin_mul_overflow(t304, t305, &r306);
                                        if (o307) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 286, 68);
                                        uint64_t t308 = r306;
                                        uint64_t t309 = (uintptr_t)t300;
                                        uint64_t t310 = (uintptr_t)t308;
                                        if (t309 > t310 || t310 > (uintptr_t)t295) gm_trap_slice(t309, t310, t295, gm_srcj10g653a00lt, 286, 48);
                                        uint64_t t311 = ((uintptr_t)t308) - (uintptr_t)t300;
                                        gm_b16_8 t312;
                                        GM_ST(void *, (uint8_t *)&t312, (void *)(((uint8_t *)t294 + (uintptr_t)t300 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t312 + 8, (uintptr_t)(t311));
                                        void * t313 = GM_LD(void *, (((uint8_t *)&F.l21) + 24));
                                        uint64_t t314 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 24) + 8));
                                        uint64_t t315 = gm_l24;
                                        uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                                        uint64_t r317; bool o318 = __builtin_mul_overflow(t315, t316, &r317);
                                        if (o318) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 286, 95);
                                        uint64_t t319 = r317;
                                        uint64_t t320 = gm_l24;
                                        uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                                        if (o322) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 286, 108);
                                        uint64_t t323 = r321;
                                        uint64_t t324 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                                        uint64_t r325; bool o326 = __builtin_mul_overflow(t323, t324, &r325);
                                        if (o326) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 286, 107);
                                        uint64_t t327 = r325;
                                        uint64_t t328 = (uintptr_t)t319;
                                        uint64_t t329 = (uintptr_t)t327;
                                        if (t328 > t329 || t329 > (uintptr_t)t314) gm_trap_slice(t328, t329, t314, gm_srcj10g653a00lt, 286, 87);
                                        uint64_t t330 = ((uintptr_t)t327) - (uintptr_t)t319;
                                        gm_b16_8 t331;
                                        GM_ST(void *, (uint8_t *)&t331, (void *)(((uint8_t *)t313 + (uintptr_t)t319 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(t330));
                                        gm_f_equal_e5vf82h((void *)&gm_l58, (void *)NULL, cx, NULL, t291, t292, t293, t312, t331);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (t332) {
                                        } else {
                                            gm_t76123gfet73g t333 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l58) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t333);
                                            goto gm_fdone;
                                        }
                                        uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 8));
                                        uint8_t t335 = (uint8_t)!t334;
                                        if (t335) {
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                            goto gm_fdone;
                                        }
                                        goto gm_lb42;
                                    }
gm_lb43:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 285, 13);
                                }
gm_lb42:;
                            }
gm_lb41:;
                            uint64_t t336 = gm_l23;
                            uint64_t r337; bool o338 = __builtin_add_overflow(t336, ((uint64_t)1ULL), &r337);
                            uint64_t t339 = r337;
                            gm_l23 = t339;
                        }
                    }
gm_lb39:;
                    {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                        goto gm_fdone;
                    }
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t341 = (uint8_t)(t340 == ((uint8_t)13ULL));
                    if (!t341) {
                        goto gm_lb44;
                    }
                    gm_t88rcucg1b7uk t342 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                    gm_l25 = t342;
                    void * t343 = gm_l0;
                    void * t344 = GM_LD(void *, (((uint8_t *)t343) + 8));
                    gm_t88rcucg1b7uk t345 = gm_l25;
                    uint8_t t346 = gm_f_nullable_pointer_7qfmn6o(t344, GM_CONV(gm_csguankko4lmhd, t345));
                    if (t346) {
                        void * t347 = gm_l0;
                        gm_b16_8 t348 = gm_l3;
                        gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                        uint64_t t350 = gm_f_ptr_of_b77b4c9(t347, t349);
                        void * t351 = gm_l0;
                        gm_b16_8 t352 = gm_l4;
                        gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
                        uint64_t t354 = gm_f_ptr_of_b77b4c9(t351, t353);
                        uint8_t t355 = ((t350 == t354));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t355);
                        goto gm_fdone;
                    }
                    void * t356 = gm_l0;
                    gm_t88rcucg1b7uk t357 = gm_l2;
                    gm_b16_8 t358 = gm_l3;
                    gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&t358));
                    uint8_t t360 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t356, t357, t359);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t360;
                    void * t361 = gm_l0;
                    gm_t88rcucg1b7uk t362 = gm_l2;
                    gm_b16_8 t363 = gm_l4;
                    gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&t363));
                    uint8_t t365 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t361, t362, t364);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t365;
                    {
                        {
                            uint8_t t366 = gm_l26;
                            uint8_t t367 = (uint8_t)!t366;
                            if (!t367) {
                                goto gm_lb46;
                            }
                            uint8_t t368 = gm_l27;
                            uint8_t t369 = (uint8_t)!t368;
                            if (!t369) {
                                goto gm_lb46;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                            goto gm_fdone;
                        }
gm_lb46:;
                    }
gm_lb45:;
                    {
                        {
                            uint8_t t370 = gm_l26;
                            if (!t370) {
                                goto gm_lb48;
                            }
                            uint8_t t371 = gm_l27;
                            if (!t371) {
                                goto gm_lb48;
                            }
                            void * t372 = gm_l0;
                            gm_tl874t2atnlh3 t373 = gm_l1;
                            gm_t88rcucg1b7uk t374 = gm_l25;
                            void * t375 = gm_l0;
                            gm_t88rcucg1b7uk t376 = gm_l2;
                            gm_b16_8 t377 = gm_l3;
                            gm_b16_8 t378 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t375, t376, t377);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t379 = gm_l0;
                            gm_t88rcucg1b7uk t380 = gm_l2;
                            gm_b16_8 t381 = gm_l4;
                            gm_b16_8 t382 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t379, t380, t381);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t372, t373, t374, t378, t382);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb48:;
                    }
gm_lb47:;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                    goto gm_fdone;
                }
gm_lb44:;
            }
            {
                {
                    uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t384 = (uint8_t)(t383 == ((uint8_t)14ULL));
                    if (!t384) {
                        goto gm_lb49;
                    }
                    gm_t88rcucg1b7uk t385 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                    gm_l28 = t385;
                    gm_t88rcucg1b7uk t386 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l46) + 8) + 4));
                    gm_l29 = t386;
                    void * t387 = gm_l0;
                    gm_t88rcucg1b7uk t388 = gm_l2;
                    gm_b16_8 t389 = gm_l3;
                    gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
                    uint8_t t391 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t387, t388, t390);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l30 = t391;
                    uint8_t t392 = gm_l30;
                    void * t393 = gm_l0;
                    gm_t88rcucg1b7uk t394 = gm_l2;
                    gm_b16_8 t395 = gm_l4;
                    gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&t395));
                    uint8_t t397 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t393, t394, t396);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t398 = (!(t392 == t397));
                    if (t398) {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                    void * t399 = gm_l0;
                    gm_tl874t2atnlh3 t400 = gm_l1;
                    uint8_t t401 = gm_l30;
                    if (t401) {
                        gm_t88rcucg1b7uk t402 = gm_l28;
                        gm_l59 = t402;
                    } else {
                        gm_t88rcucg1b7uk t403 = gm_l29;
                        gm_l59 = t403;
                    }
                    gm_t88rcucg1b7uk t404 = gm_l59;
                    void * t405 = gm_l0;
                    gm_t88rcucg1b7uk t406 = gm_l2;
                    gm_b16_8 t407 = gm_l3;
                    gm_b16_8 t408 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t405, t406, t407);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t409 = gm_l0;
                    gm_t88rcucg1b7uk t410 = gm_l2;
                    gm_b16_8 t411 = gm_l4;
                    gm_b16_8 t412 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t409, t410, t411);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t399, t400, t404, t408, t412);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb49:;
                {
                    uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                    uint8_t t414 = (uint8_t)(t413 == ((uint8_t)17ULL));
                    if (!t414) {
                        goto gm_lb50;
                    }
                    {
                        {
                            void * t415 = gm_l0;
                            gm_t88rcucg1b7uk t416 = gm_l2;
                            gm_tkn42u7na3t81 t417 = gm_f_bits_struct_ty_22ue20s(t415, GM_CONV(gm_csguankko4lmhd, t416));
                            gm_l60 = t417;
                            uint8_t t418 = (uint8_t)((((uint8_t *)&gm_l60))[4] != 0);
                            if (!t418) {
                                goto gm_lb52;
                            }
                            gm_t88rcucg1b7uk t419 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l60));
                            gm_l31 = t419;
                            void * t420 = gm_l0;
                            gm_tl874t2atnlh3 t421 = gm_l1;
                            gm_t88rcucg1b7uk t422 = gm_l31;
                            gm_b16_8 t423 = gm_l3;
                            gm_b16_8 t424 = gm_l4;
                            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t420, t421, t422, t423, t424);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb52:;
                    }
gm_lb51:;
                    {
                        {
                            {
                                void * t425 = gm_l0;
                                gm_t88rcucg1b7uk t426 = gm_l2;
                                gm_f_union_def_h8mvf4t((void *)&gm_l61, t425, GM_CONV(gm_csguankko4lmhd, t426));
                            }
                            uint8_t t427 = (uint8_t)((((uint8_t *)&gm_l61))[24] != 0);
                            if (!t427) {
                                goto gm_lb54;
                            }
                            {
                                memmove(((uint8_t *)&gm_l32), ((uint8_t *)&gm_l61), 24);
                            }
                            void * t428 = gm_l0;
                            gm_t88rcucg1b7uk t429 = gm_l2;
                            gm_b16_8 t430 = gm_l3;
                            gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&t430));
                            gm_t9805qtonbsag t432 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t428, t429, t431);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l62 = t432;
                            uint8_t t433 = (uint8_t)((((uint8_t *)&gm_l62))[4] != 0);
                            if (t433) {
                                uint32_t t434 = GM_LD(uint32_t, ((uint8_t *)&gm_l62));
                                gm_l33 = t434;
                            } else {
                                void * t435 = gm_l0;
                                gm_tl874t2atnlh3 t436 = gm_l1;
                                gm_t88rcucg1b7uk t437 = gm_l2;
                                gm_b16_8 t438 = gm_l3;
                                gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&t438));
                                gm_t76123gfet73g t440;
                                gm_f_invalid_tag_1gif354((void *)&t440, (void *)NULL, cx, NULL, t435, t436, t437, t439);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t440);
                                goto gm_fdone;
                            }
                            void * t441 = gm_l0;
                            gm_t88rcucg1b7uk t442 = gm_l2;
                            gm_b16_8 t443 = gm_l4;
                            gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
                            gm_t9805qtonbsag t445 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t441, t442, t444);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l63 = t445;
                            uint8_t t446 = (uint8_t)((((uint8_t *)&gm_l63))[4] != 0);
                            if (t446) {
                                uint32_t t447 = GM_LD(uint32_t, ((uint8_t *)&gm_l63));
                                gm_l34 = t447;
                            } else {
                                void * t448 = gm_l0;
                                gm_tl874t2atnlh3 t449 = gm_l1;
                                gm_t88rcucg1b7uk t450 = gm_l2;
                                gm_b16_8 t451 = gm_l4;
                                gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
                                gm_t76123gfet73g t453;
                                gm_f_invalid_tag_1gif354((void *)&t453, (void *)NULL, cx, NULL, t448, t449, t450, t452);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t453);
                                goto gm_fdone;
                            }
                            uint32_t t454 = gm_l33;
                            uint32_t t455 = gm_l34;
                            uint8_t t456 = (!(t454 == t455));
                            if (t456) {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                goto gm_fdone;
                            }
                            void * t457 = GM_LD(void *, (((uint8_t *)&gm_l32) + 8));
                            uint64_t t458 = GM_LD(uint64_t, ((((uint8_t *)&gm_l32) + 8) + 8));
                            uint32_t t459 = gm_l33;
                            uint64_t t460 = (uint64_t)t459;
                            uint64_t t461 = gm_f_us_cuv01fp(t460);
                            if ((uintptr_t)t461 >= (uintptr_t)(t458)) gm_trap_index((uintptr_t)t461, t458, gm_srcj10g653a00lt, 309, 23);
                            uint8_t t462 = (uint8_t)((((((uint8_t *)t457) + (uintptr_t)t461 * 72) + 16))[4] != 0);
                            if (t462) {
                                gm_t88rcucg1b7uk t463 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t457) + (uintptr_t)t461 * 72) + 16));
                                gm_l35 = t463;
                            } else {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                                goto gm_fdone;
                            }
                            void * t464 = gm_l0;
                            gm_t88rcucg1b7uk t465 = gm_l2;
                            uint64_t t466 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t464, t465);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t467 = gm_f_us_cuv01fp(t466);
                            gm_l36 = t467;
                            void * t468 = gm_l0;
                            gm_t88rcucg1b7uk t469 = gm_l35;
                            uint64_t t470 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t468, t469);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t471 = gm_f_us_cuv01fp(t470);
                            gm_l37 = t471;
                            {
                                void * t472 = gm_l0;
                                gm_tl874t2atnlh3 t473 = gm_l1;
                                gm_t88rcucg1b7uk t474 = gm_l35;
                                void * t475 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t476 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t477 = gm_l36;
                                uint64_t t478 = gm_l36;
                                uint64_t t479 = gm_l37;
                                uint64_t r480; bool o481 = __builtin_add_overflow(t478, t479, &r480);
                                if (o481) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 312, 49);
                                uint64_t t482 = r480;
                                uint64_t t483 = (uintptr_t)t477;
                                uint64_t t484 = (uintptr_t)t482;
                                if (t483 > t484 || t484 > (uintptr_t)t476) gm_trap_slice(t483, t484, t476, gm_srcj10g653a00lt, 312, 42);
                                uint64_t t485 = ((uintptr_t)t482) - (uintptr_t)t477;
                                gm_b16_8 t486;
                                GM_ST(void *, (uint8_t *)&t486, (void *)(((uint8_t *)t475 + (uintptr_t)t477 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t486 + 8, (uintptr_t)(t485));
                                void * t487 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t488 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t489 = gm_l36;
                                uint64_t t490 = gm_l36;
                                uint64_t t491 = gm_l37;
                                uint64_t r492; bool o493 = __builtin_add_overflow(t490, t491, &r492);
                                if (o493) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 312, 66);
                                uint64_t t494 = r492;
                                uint64_t t495 = (uintptr_t)t489;
                                uint64_t t496 = (uintptr_t)t494;
                                if (t495 > t496 || t496 > (uintptr_t)t488) gm_trap_slice(t495, t496, t488, gm_srcj10g653a00lt, 312, 59);
                                uint64_t t497 = ((uintptr_t)t494) - (uintptr_t)t489;
                                gm_b16_8 t498;
                                GM_ST(void *, (uint8_t *)&t498, (void *)(((uint8_t *)t487 + (uintptr_t)t489 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t498 + 8, (uintptr_t)(t497));
                                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t472, t473, t474, t486, t498);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                        }
gm_lb54:;
                    }
gm_lb53:;
                    void * t499 = gm_l0;
                    gm_t88rcucg1b7uk t500 = gm_l2;
                    void * t501 = gm_f_adt_of_8k5vqud(t499, GM_CONV(gm_csguankko4lmhd, t500));
                    gm_l64 = t501;
                    uint8_t t502 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l64)) != 0);
                    if (t502) {
                        void * t503 = GM_LD(void *, ((uint8_t *)&gm_l64));
                        gm_l38 = t503;
                    } else {
                        void * t504 = gm_l0;
                        gm_tl874t2atnlh3 t505 = gm_l1;
                        void * t506 = gm_l0;
                        void * t507 = GM_LD(void *, (((uint8_t *)t506) + 8));
                        gm_t88rcucg1b7uk t508 = gm_l2;
                        gm_b16_8 t509 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t507, t508);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t510;
                        GM_ST(gm_b16_8, ((uint8_t *)&t510 + 0), t509);
                        gm_f_format_h8e7ps1((void *)&gm_l65, (void *)NULL, cx, NULL, t510);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t511 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (!t511) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srcj10g653a00lt, 314, 56);
                        gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                        gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
                        gm_t76123gfet73g t514;
                        gm_f_trap_qh84l9d((void *)&t514, (void *)NULL, cx, NULL, t504, t505, t513);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t514);
                        goto gm_fdone;
                    }
                    {
                        {
                            {
                                void * t515 = gm_l38;
                                uint8_t t516 = GM_LD(uint8_t, ((((uint8_t *)t515) + 24) + 0));
                                uint8_t t517 = (uint8_t)(t516 == ((uint8_t)0ULL));
                                if (!t517) {
                                    goto gm_lb56;
                                }
                                memmove(((uint8_t *)&F.l39), ((((uint8_t *)t515) + 24) + 8), 48);
                            }
                            gm_l41 = ((uint64_t)0ULL);
                            uint64_t t518 = GM_LD(uint64_t, (((uint8_t *)&F.l39) + 8));
                            gm_l40 = t518;
                            {
                                for (;;) {
                                    uint64_t t519 = gm_l41;
                                    uint64_t t520 = gm_l40;
                                    uint8_t t521 = (t519 < t520);
                                    if (!t521) {
                                        goto gm_lb57;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t522 = gm_l41;
                                                gm_l42 = t522;
                                                void * t523 = gm_l0;
                                                gm_t88rcucg1b7uk t524 = gm_l2;
                                                uint64_t t525 = gm_l42;
                                                uint32_t t526 = (uint32_t)t525;
                                                gm_tfbnciphf3261 t527 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t523, t524, t526);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l43 = t527;
                                                uint64_t t528 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                uint64_t t529 = gm_f_us_cuv01fp(t528);
                                                gm_l44 = t529;
                                                void * t530 = gm_l0;
                                                gm_t88rcucg1b7uk t531 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l43));
                                                uint64_t t532 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t530, t531);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint64_t t533 = gm_f_us_cuv01fp(t532);
                                                gm_l45 = t533;
                                                void * t534 = gm_l0;
                                                gm_tl874t2atnlh3 t535 = gm_l1;
                                                gm_t88rcucg1b7uk t536 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l43));
                                                void * t537 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                uint64_t t538 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                uint64_t t539 = gm_l44;
                                                uint64_t t540 = gm_l44;
                                                uint64_t t541 = gm_l45;
                                                uint64_t r542; bool o543 = __builtin_add_overflow(t540, t541, &r542);
                                                if (o543) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 320, 56);
                                                uint64_t t544 = r542;
                                                uint64_t t545 = (uintptr_t)t539;
                                                uint64_t t546 = (uintptr_t)t544;
                                                if (t545 > t546 || t546 > (uintptr_t)t538) gm_trap_slice(t545, t546, t538, gm_srcj10g653a00lt, 320, 49);
                                                uint64_t t547 = ((uintptr_t)t544) - (uintptr_t)t539;
                                                gm_b16_8 t548;
                                                GM_ST(void *, (uint8_t *)&t548, (void *)(((uint8_t *)t537 + (uintptr_t)t539 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t548 + 8, (uintptr_t)(t547));
                                                void * t549 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t550 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t551 = gm_l44;
                                                uint64_t t552 = gm_l44;
                                                uint64_t t553 = gm_l45;
                                                uint64_t r554; bool o555 = __builtin_add_overflow(t552, t553, &r554);
                                                if (o555) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 320, 73);
                                                uint64_t t556 = r554;
                                                uint64_t t557 = (uintptr_t)t551;
                                                uint64_t t558 = (uintptr_t)t556;
                                                if (t557 > t558 || t558 > (uintptr_t)t550) gm_trap_slice(t557, t558, t550, gm_srcj10g653a00lt, 320, 66);
                                                uint64_t t559 = ((uintptr_t)t556) - (uintptr_t)t551;
                                                gm_b16_8 t560;
                                                GM_ST(void *, (uint8_t *)&t560, (void *)(((uint8_t *)t549 + (uintptr_t)t551 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t560 + 8, (uintptr_t)(t559));
                                                gm_f_equal_e5vf82h((void *)&gm_l66, (void *)NULL, cx, NULL, t534, t535, t536, t548, t560);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t561 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                if (t561) {
                                                } else {
                                                    gm_t76123gfet73g t562 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l66) + 8));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t562);
                                                    goto gm_fdone;
                                                }
                                                uint8_t t563 = GM_LD(uint8_t, (((uint8_t *)&gm_l66) + 8));
                                                uint8_t t564 = (uint8_t)!t563;
                                                if (t564) {
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb60;
                                            }
gm_lb61:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 316, 13);
                                        }
gm_lb60:;
                                    }
gm_lb59:;
                                    uint64_t t565 = gm_l41;
                                    uint64_t r566; bool o567 = __builtin_add_overflow(t565, ((uint64_t)1ULL), &r566);
                                    uint64_t t568 = r566;
                                    gm_l41 = t568;
                                }
                            }
gm_lb57:;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)1));
                                goto gm_fdone;
                            }
                        }
gm_lb56:;
                        {
                            void * t569 = gm_l0;
                            gm_tl874t2atnlh3 t570 = gm_l1;
                            void * t571 = gm_l0;
                            void * t572 = GM_LD(void *, (((uint8_t *)t571) + 8));
                            gm_t88rcucg1b7uk t573 = gm_l2;
                            gm_b16_8 t574 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t572, t573);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t575;
                            GM_ST(gm_b16_8, ((uint8_t *)&t575 + 0), t574);
                            gm_f_format_dmukbli((void *)&gm_l67, (void *)NULL, cx, NULL, t575);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t576 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                            if (!t576) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srcj10g653a00lt, 315, 62);
                            gm_b16_8 t577 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                            gm_b16_8 t578 = GM_LD(gm_b16_8, ((uint8_t *)&t577));
                            gm_t76123gfet73g t579;
                            gm_f_trap_qh84l9d((void *)&t579, (void *)NULL, cx, NULL, t569, t570, t578);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t579);
                            goto gm_fdone;
                        }
                    }
gm_lb55:;
                }
gm_lb50:;
            }
            {
                {
                    void * t580 = gm_l0;
                    gm_tl874t2atnlh3 t581 = gm_l1;
                    void * t582 = gm_l0;
                    void * t583 = GM_LD(void *, (((uint8_t *)t582) + 8));
                    gm_t88rcucg1b7uk t584 = gm_l2;
                    gm_b16_8 t585 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t583, t584);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t586;
                    GM_ST(gm_b16_8, ((uint8_t *)&t586 + 0), t585);
                    gm_f_format_dmukbli((void *)&gm_l68, (void *)NULL, cx, NULL, t586);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t587 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                    if (!t587) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_srcj10g653a00lt, 324, 35);
                    gm_b16_8 t588 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                    gm_b16_8 t589 = GM_LD(gm_b16_8, ((uint8_t *)&t588));
                    gm_t76123gfet73g t590;
                    gm_f_trap_qh84l9d((void *)&t590, (void *)NULL, cx, NULL, t580, t581, t589);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t590);
                    goto gm_fdone;
                }
gm_lb62:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 253, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_1gif354 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_tcmkompbl09il gm_l4; /* u */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t57euh612ligr gm_l5;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_f_union_def_h8mvf4t((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
    if (t4) {
        memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l5), 24);
    } else {
        void * t5 = gm_l0;
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s7ah1q5ah26rj));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(11));
        gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        void * t8 = gm_l0;
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l2;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
        gm_b16_8 t13 = gm_l3;
        gm_b16_8 t14 = gm_f_invalid_tag_text_f3l5hi7((void *)NULL, cx, NULL, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t8, t9, t14);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_text_f3l5hi7 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* tag */
    gm_b16_8 gm_l3; /* b */
    unsigned __int128 gm_l4; /* raw */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* text */
    GM_STACK_CHECK(&F, 573u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tg5lql6itufk7 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l2;
    uint64_t t8 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t11 = (uintptr_t)t9;
    if (t10 > t11 || t11 > (uintptr_t)t5) gm_trap_slice(t10, t11, t5, gm_srcj10g653a00lt, 337, 27);
    uint64_t t12 = ((uintptr_t)t9) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12));
    unsigned __int128 t14 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t13);
    gm_l4 = t14;
    void * t15 = gm_l0;
    gm_t88rcucg1b7uk t16 = gm_l2;
    gm_tg5lql6itufk7 t17 = gm_f_int_info_ulcevdv(t15, GM_CONV(gm_csguankko4lmhd, t16));
    gm_l7 = t17;
    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
    if (t18) {
        gm_ttovhfee8mf28 t19 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l7));
        gm_l5 = t19;
    } else {
        gm_ttovhfee8mf28 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0));
        GM_ST(uint32_t, ((uint8_t *)&t20 + 4), ((uint32_t)8ULL));
        gm_l5 = t20;
    }
    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
    if (t21) {
        unsigned __int128 t22 = gm_l4;
        __int128 t23;
        memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 16 ? sizeof t23 : 16);
        gm_tpl3q6e8s9qo1 t24;
        GM_ST(__int128, ((uint8_t *)&t24 + 0), t23);
        gm_f_format_9qvncot((void *)&gm_l8, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcj10g653a00lt, 339, 30);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_l6 = t26;
    } else {
        unsigned __int128 t27 = gm_l4;
        gm_tsn2omssg6cpf t28;
        GM_ST(unsigned __int128, ((uint8_t *)&t28 + 0), t27);
        gm_f_format_ra7pont((void *)&gm_l9, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcj10g653a00lt, 339, 73);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l6 = t30;
    }
    {
        gm_b16_8 t31 = gm_l6;
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
        gm_t88rcucg1b7uk t34 = gm_l1;
        gm_b16_8 t35 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 16), t35);
        gm_f_format_mq7e94u((void *)&gm_l10, (void *)NULL, cx, NULL, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcj10g653a00lt, 340, 12);
    gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
    F.ret = t39;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_cmp_r2i4hv9 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 14u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    {
        {
            {
                {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)10ULL));
                    if (!t11) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)11ULL));
                    if (!t13) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)12ULL));
                    if (!t15) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)13ULL));
                    if (!t17) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
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
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 26, 40);
    }
gm_lb1:;
    uint8_t t18 = gm_l1;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_less_nbfe87l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    unsigned __int128 gm_l6; /* a */
    unsigned __int128 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l12; /* fa */
    gm_b16_8 gm_l13; /* fb */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* xs */
    gm_b16_8 gm_l21; /* ys */
    gm_tcmkompbl09il gm_l22; /* u */
    gm_ttovhfee8mf28 gm_l23; /* info */
    uint64_t gm_l24; /* n */
    unsigned __int128 gm_l25; /* a */
    unsigned __int128 gm_l26; /* c */
    gm_t88rcucg1b7uk gm_l27; /* u */
    GM_STACK_CHECK(&F, 2918u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l28;
    gm_tg5lql6itufk7 gm_l29;
    gm_t1ec8br9ddkav gm_l30;
    gm_t1ec8br9ddkav gm_l31;
    gm_tmc91gv7n9c0i gm_l32;
    gm_tmc91gv7n9c0i gm_l33;
    gm_t1ec8br9ddkav gm_l34;
    gm_t57euh612ligr gm_l35;
    gm_tg5lql6itufk7 gm_l36;
    gm_t1ec8br9ddkav gm_l37;
    gm_tkn42u7na3t81 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tabb710d9mq2e gm_l40;
    {
        void * t37 = gm_l0;
        gm_t88rcucg1b7uk t38 = gm_l2;
        gm_f_kind_htv6093((void *)&gm_l28, t37, GM_CONV(gm_csguankko4lmhd, t38));
        {
            {
                {
                    {
                        {
                            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                            if (!t40) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)1ULL));
                            if (!t42) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)2ULL));
                            if (!t44) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t45 = gm_l0;
                    gm_t88rcucg1b7uk t46 = gm_l2;
                    gm_tg5lql6itufk7 t47 = gm_f_int_info_ulcevdv(t45, GM_CONV(gm_csguankko4lmhd, t46));
                    gm_l29 = t47;
                    uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l29))[8] != 0);
                    if (t48) {
                        gm_ttovhfee8mf28 t49 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l29));
                        gm_l5 = t49;
                    } else {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                        goto gm_fdone;
                    }
                    void * t50 = gm_l0;
                    gm_ttovhfee8mf28 t51 = gm_l5;
                    gm_b16_8 t52 = gm_l3;
                    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                    unsigned __int128 t54 = gm_f_rd_int_iuioodt(t50, t51, t53);
                    gm_l6 = t54;
                    void * t55 = gm_l0;
                    gm_ttovhfee8mf28 t56 = gm_l5;
                    gm_b16_8 t57 = gm_l4;
                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                    unsigned __int128 t59 = gm_f_rd_int_iuioodt(t55, t56, t58);
                    gm_l7 = t59;
                    uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
                    if (t60) {
                        unsigned __int128 t61 = gm_l6;
                        __int128 t62;
                        memset(&t62, 0, sizeof t62); memcpy(&t62, ((uint8_t *)&t61), sizeof t62 < 16 ? sizeof t62 : 16);
                        unsigned __int128 t63 = gm_l7;
                        __int128 t64;
                        memset(&t64, 0, sizeof t64); memcpy(&t64, ((uint8_t *)&t63), sizeof t64 < 16 ? sizeof t64 : 16);
                        uint8_t t65 = (t62 < t64);
                        GM_ST(uint8_t, (uint8_t *)&gm_l30 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l30 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l30 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l30 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l30 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l30 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l30 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l30 + 24, 0);
                        ((uint8_t *)&gm_l30)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l30 + 8), t65);
                    } else {
                        unsigned __int128 t66 = gm_l6;
                        unsigned __int128 t67 = gm_l7;
                        uint8_t t68 = (t66 < t67);
                        GM_ST(uint8_t, (uint8_t *)&gm_l30 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l30 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l30 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&gm_l30 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l30 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l30 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l30 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l30 + 24, 0);
                        ((uint8_t *)&gm_l30)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&gm_l30 + 8), t68);
                    }
                    memmove(gm_out, ((uint8_t *)&gm_l30), 32);
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)3ULL));
                    if (!t70) {
                        goto gm_lb7;
                    }
                    void * t71 = gm_l0;
                    gm_t88rcucg1b7uk t72 = gm_l2;
                    gm_b16_8 t73 = gm_l3;
                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                    double t75 = gm_f_get_float_grmhlm4(t71, GM_CONV(gm_csguankko4lmhd, t72), t74);
                    void * t76 = gm_l0;
                    gm_t88rcucg1b7uk t77 = gm_l2;
                    gm_b16_8 t78 = gm_l4;
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    double t80 = gm_f_get_float_grmhlm4(t76, GM_CONV(gm_csguankko4lmhd, t77), t79);
                    uint8_t t81 = (t75 < t80);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t81);
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)4ULL));
                    if (!t83) {
                        goto gm_lb8;
                    }
                    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t85, gm_srcj10g653a00lt, 210, 25);
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t84) + (uintptr_t)((uint64_t)0ULL) * 1));
                    void * t87 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t88, gm_srcj10g653a00lt, 210, 32);
                    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)t87) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t90 = (t86 < t89);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t90);
                    goto gm_fdone;
                }
gm_lb8:;
                {
                    {
                        {
                            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)8ULL));
                            if (!t92) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t94 = (uint8_t)(t93 == ((uint8_t)9ULL));
                            if (!t94) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)7ULL));
                            if (!t96) {
                                goto gm_lb13;
                            }
                            goto gm_lb10;
                        }
gm_lb13:;
                        {
                            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)16ULL));
                            if (!t98) {
                                goto gm_lb14;
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t99 = gm_l0;
                    gm_b16_8 t100 = gm_l3;
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                    uint64_t t102 = gm_f_ptr_of_b77b4c9(t99, t101);
                    void * t103 = gm_l0;
                    gm_b16_8 t104 = gm_l4;
                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                    uint64_t t106 = gm_f_ptr_of_b77b4c9(t103, t105);
                    uint8_t t107 = (t102 < t106);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t107);
                    goto gm_fdone;
                }
gm_lb9:;
                {
                    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t109 = (uint8_t)(t108 == ((uint8_t)12ULL));
                    if (!t109) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                    gm_l8 = t110;
                    gm_l10 = ((uint64_t)0ULL);
                    uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    gm_l9 = t111;
                    {
                        for (;;) {
                            uint64_t t112 = gm_l10;
                            uint64_t t113 = gm_l9;
                            uint8_t t114 = (t112 < t113);
                            if (!t114) {
                                goto gm_lb16;
                            }
                            {
                                {
                                    {
                                        uint64_t t115 = gm_l10;
                                        gm_l11 = t115;
                                        void * t116 = gm_l0;
                                        gm_t88rcucg1b7uk t117 = gm_l2;
                                        gm_b16_8 t118 = gm_l3;
                                        uint64_t t119 = gm_l11;
                                        uint32_t t120 = (uint32_t)t119;
                                        gm_b16_8 t121 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t116, t117, t118, t120);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l12 = t121;
                                        void * t122 = gm_l0;
                                        gm_t88rcucg1b7uk t123 = gm_l2;
                                        gm_b16_8 t124 = gm_l4;
                                        uint64_t t125 = gm_l11;
                                        uint32_t t126 = (uint32_t)t125;
                                        gm_b16_8 t127 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t122, t123, t124, t126);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l13 = t127;
                                        void * t128 = gm_l0;
                                        gm_tl874t2atnlh3 t129 = gm_l1;
                                        void * t130 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                        uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        uint64_t t132 = gm_l11;
                                        gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t130) + (uintptr_t)t132 * 4));
                                        gm_b16_8 t134 = gm_l12;
                                        gm_b16_8 t135 = gm_l13;
                                        gm_f_equal_e5vf82h((void *)&gm_l31, (void *)NULL, cx, NULL, t128, t129, t133, t134, t135);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                        if (t136) {
                                        } else {
                                            gm_t76123gfet73g t137 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l31) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t137);
                                            goto gm_fdone;
                                        }
                                        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 8));
                                        uint8_t t139 = (uint8_t)!t138;
                                        if (t139) {
                                            void * t140 = gm_l0;
                                            gm_tl874t2atnlh3 t141 = gm_l1;
                                            void * t142 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                            uint64_t t144 = gm_l11;
                                            gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + (uintptr_t)t144 * 4));
                                            gm_b16_8 t146 = gm_l12;
                                            gm_b16_8 t147 = gm_l13;
                                            gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t140, t141, t145, t146, t147);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_fdone;
                                        }
                                        goto gm_lb19;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 213, 13);
                                }
gm_lb19:;
                            }
gm_lb18:;
                            uint64_t t148 = gm_l10;
                            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                            uint64_t t151 = r149;
                            gm_l10 = t151;
                        }
                    }
gm_lb16:;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                    goto gm_fdone;
                }
gm_lb15:;
            }
            {
                {
                    {
                        {
                            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t153 = (uint8_t)(t152 == ((uint8_t)11ULL));
                            if (!t153) {
                                goto gm_lb23;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                        {
                            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t155 = (uint8_t)(t154 == ((uint8_t)10ULL));
                            if (!t155) {
                                goto gm_lb24;
                            }
                            goto gm_lb22;
                        }
gm_lb24:;
                        goto gm_lb21;
                    }
gm_lb22:;
                    {
                        void * t156 = gm_l0;
                        gm_tl874t2atnlh3 t157 = gm_l1;
                        gm_t88rcucg1b7uk t158 = gm_l2;
                        gm_b16_8 t159 = gm_l3;
                        gm_f_seq_of_ifahtjs((void *)&gm_l32, (void *)NULL, cx, NULL, t156, t157, t158, t159);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (t160) {
                    } else {
                        gm_t76123gfet73g t161 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l32) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t161);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l32) + 8), 40);
                    }
                    {
                        void * t162 = gm_l0;
                        gm_tl874t2atnlh3 t163 = gm_l1;
                        gm_t88rcucg1b7uk t164 = gm_l2;
                        gm_b16_8 t165 = gm_l4;
                        gm_f_seq_of_ifahtjs((void *)&gm_l33, (void *)NULL, cx, NULL, t162, t163, t164, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (t166) {
                    } else {
                        gm_t76123gfet73g t167 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l33) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t167);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l15), (((uint8_t *)&gm_l33) + 8), 40);
                    }
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
                    uint64_t t170 = (t168 < t169 ? t168 : t169);
                    gm_l16 = t170;
                    gm_l18 = ((uint64_t)0ULL);
                    uint64_t t171 = gm_l16;
                    uint64_t t172 = gm_f_us_cuv01fp(t171);
                    gm_l17 = t172;
                    {
                        for (;;) {
                            uint64_t t173 = gm_l18;
                            uint64_t t174 = gm_l17;
                            uint8_t t175 = (t173 < t174);
                            if (!t175) {
                                goto gm_lb25;
                            }
                            {
                                {
                                    {
                                        uint64_t t176 = gm_l18;
                                        gm_l19 = t176;
                                        void * t177 = GM_LD(void *, (((uint8_t *)&F.l14) + 24));
                                        uint64_t t178 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 24) + 8));
                                        uint64_t t179 = gm_l19;
                                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t r181; bool o182 = __builtin_mul_overflow(t179, t180, &r181);
                                        if (o182) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 225, 31);
                                        uint64_t t183 = r181;
                                        uint64_t t184 = gm_l19;
                                        uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                        if (o186) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 225, 44);
                                        uint64_t t187 = r185;
                                        uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t r189; bool o190 = __builtin_mul_overflow(t187, t188, &r189);
                                        if (o190) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 225, 43);
                                        uint64_t t191 = r189;
                                        uint64_t t192 = (uintptr_t)t183;
                                        uint64_t t193 = (uintptr_t)t191;
                                        if (t192 > t193 || t193 > (uintptr_t)t178) gm_trap_slice(t192, t193, t178, gm_srcj10g653a00lt, 225, 23);
                                        uint64_t t194 = ((uintptr_t)t191) - (uintptr_t)t183;
                                        gm_b16_8 t195;
                                        GM_ST(void *, (uint8_t *)&t195, (void *)(((uint8_t *)t177 + (uintptr_t)t183 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(t194));
                                        gm_l20 = t195;
                                        void * t196 = GM_LD(void *, (((uint8_t *)&F.l15) + 24));
                                        uint64_t t197 = GM_LD(uint64_t, ((((uint8_t *)&F.l15) + 24) + 8));
                                        uint64_t t198 = gm_l19;
                                        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t r200; bool o201 = __builtin_mul_overflow(t198, t199, &r200);
                                        if (o201) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 226, 31);
                                        uint64_t t202 = r200;
                                        uint64_t t203 = gm_l19;
                                        uint64_t r204; bool o205 = __builtin_add_overflow(t203, ((uint64_t)1ULL), &r204);
                                        if (o205) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 226, 44);
                                        uint64_t t206 = r204;
                                        uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t r208; bool o209 = __builtin_mul_overflow(t206, t207, &r208);
                                        if (o209) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 226, 43);
                                        uint64_t t210 = r208;
                                        uint64_t t211 = (uintptr_t)t202;
                                        uint64_t t212 = (uintptr_t)t210;
                                        if (t211 > t212 || t212 > (uintptr_t)t197) gm_trap_slice(t211, t212, t197, gm_srcj10g653a00lt, 226, 23);
                                        uint64_t t213 = ((uintptr_t)t210) - (uintptr_t)t202;
                                        gm_b16_8 t214;
                                        GM_ST(void *, (uint8_t *)&t214, (void *)(((uint8_t *)t196 + (uintptr_t)t202 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(t213));
                                        gm_l21 = t214;
                                        void * t215 = gm_l0;
                                        gm_tl874t2atnlh3 t216 = gm_l1;
                                        gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l14));
                                        gm_b16_8 t218 = gm_l20;
                                        gm_b16_8 t219 = gm_l21;
                                        gm_f_equal_e5vf82h((void *)&gm_l34, (void *)NULL, cx, NULL, t215, t216, t217, t218, t219);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                        if (t220) {
                                        } else {
                                            gm_t76123gfet73g t221 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l34) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t221);
                                            goto gm_fdone;
                                        }
                                        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l34) + 8));
                                        uint8_t t223 = (uint8_t)!t222;
                                        if (t223) {
                                            void * t224 = gm_l0;
                                            gm_tl874t2atnlh3 t225 = gm_l1;
                                            gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l14));
                                            gm_b16_8 t227 = gm_l20;
                                            gm_b16_8 t228 = gm_l21;
                                            gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t224, t225, t226, t227, t228);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_fdone;
                                        }
                                        goto gm_lb28;
                                    }
gm_lb29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 224, 13);
                                }
gm_lb28:;
                            }
gm_lb27:;
                            uint64_t t229 = gm_l18;
                            uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                            uint64_t t232 = r230;
                            gm_l18 = t232;
                        }
                    }
gm_lb25:;
                    {
                        uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
                        uint8_t t235 = (t233 < t234);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t235);
                        goto gm_fdone;
                    }
                }
gm_lb21:;
            }
            {
                {
                    uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t237 = (uint8_t)(t236 == ((uint8_t)17ULL));
                    if (!t237) {
                        goto gm_lb30;
                    }
                    {
                        {
                            {
                                void * t238 = gm_l0;
                                gm_t88rcucg1b7uk t239 = gm_l2;
                                gm_f_union_def_h8mvf4t((void *)&gm_l35, t238, GM_CONV(gm_csguankko4lmhd, t239));
                            }
                            uint8_t t240 = (uint8_t)((((uint8_t *)&gm_l35))[24] != 0);
                            if (!t240) {
                                goto gm_lb32;
                            }
                            {
                                memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l35), 24);
                            }
                            void * t241 = gm_l0;
                            gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                            gm_tg5lql6itufk7 t243 = gm_f_int_info_ulcevdv(t241, GM_CONV(gm_csguankko4lmhd, t242));
                            gm_l36 = t243;
                            uint8_t t244 = (uint8_t)((((uint8_t *)&gm_l36))[8] != 0);
                            if (t244) {
                                gm_ttovhfee8mf28 t245 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l36));
                                gm_l23 = t245;
                            } else {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), ((uint8_t)0));
                                goto gm_fdone;
                            }
                            void * t246 = gm_l0;
                            gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                            uint64_t t248 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t246, t247);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t249 = gm_f_us_cuv01fp(t248);
                            gm_l24 = t249;
                            void * t250 = gm_l0;
                            gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                            void * t252 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t254 = gm_l24;
                            uint64_t t255 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t256 = (uintptr_t)t254;
                            if (t255 > t256 || t256 > (uintptr_t)t253) gm_trap_slice(t255, t256, t253, gm_srcj10g653a00lt, 235, 39);
                            uint64_t t257 = ((uintptr_t)t254) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t258;
                            GM_ST(void *, (uint8_t *)&t258, (void *)(((uint8_t *)t252 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t258 + 8, (uintptr_t)(t257));
                            gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                            unsigned __int128 t260 = gm_f_get_int_pg7p4gc(t250, GM_CONV(gm_csguankko4lmhd, t251), t259);
                            gm_l25 = t260;
                            void * t261 = gm_l0;
                            gm_t88rcucg1b7uk t262 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                            void * t263 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t265 = gm_l24;
                            uint64_t t266 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t267 = (uintptr_t)t265;
                            if (t266 > t267 || t267 > (uintptr_t)t264) gm_trap_slice(t266, t267, t264, gm_srcj10g653a00lt, 236, 39);
                            uint64_t t268 = ((uintptr_t)t265) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t269;
                            GM_ST(void *, (uint8_t *)&t269, (void *)(((uint8_t *)t263 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(t268));
                            gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                            unsigned __int128 t271 = gm_f_get_int_pg7p4gc(t261, GM_CONV(gm_csguankko4lmhd, t262), t270);
                            gm_l26 = t271;
                            uint8_t t272 = GM_LD(uint8_t, ((uint8_t *)&gm_l23));
                            if (t272) {
                                unsigned __int128 t273 = gm_l25;
                                __int128 t274;
                                memset(&t274, 0, sizeof t274); memcpy(&t274, ((uint8_t *)&t273), sizeof t274 < 16 ? sizeof t274 : 16);
                                unsigned __int128 t275 = gm_l26;
                                __int128 t276;
                                memset(&t276, 0, sizeof t276); memcpy(&t276, ((uint8_t *)&t275), sizeof t276 < 16 ? sizeof t276 : 16);
                                uint8_t t277 = (t274 < t276);
                                GM_ST(uint8_t, (uint8_t *)&gm_l37 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l37 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l37 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&gm_l37 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l37 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l37 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&gm_l37 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&gm_l37 + 24, 0);
                                ((uint8_t *)&gm_l37)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&gm_l37 + 8), t277);
                            } else {
                                unsigned __int128 t278 = gm_l25;
                                unsigned __int128 t279 = gm_l26;
                                uint8_t t280 = (t278 < t279);
                                GM_ST(uint8_t, (uint8_t *)&gm_l37 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l37 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l37 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&gm_l37 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l37 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l37 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&gm_l37 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&gm_l37 + 24, 0);
                                ((uint8_t *)&gm_l37)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&gm_l37 + 8), t280);
                            }
                            {
                                memmove(gm_out, ((uint8_t *)&gm_l37), 32);
                                goto gm_fdone;
                            }
                        }
gm_lb32:;
                    }
gm_lb31:;
                    {
                        {
                            void * t281 = gm_l0;
                            gm_t88rcucg1b7uk t282 = gm_l2;
                            gm_tkn42u7na3t81 t283 = gm_f_bits_struct_ty_22ue20s(t281, GM_CONV(gm_csguankko4lmhd, t282));
                            gm_l38 = t283;
                            uint8_t t284 = (uint8_t)((((uint8_t *)&gm_l38))[4] != 0);
                            if (!t284) {
                                goto gm_lb34;
                            }
                            gm_t88rcucg1b7uk t285 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l38));
                            gm_l27 = t285;
                            void * t286 = gm_l0;
                            gm_tl874t2atnlh3 t287 = gm_l1;
                            gm_t88rcucg1b7uk t288 = gm_l27;
                            gm_b16_8 t289 = gm_l3;
                            gm_b16_8 t290 = gm_l4;
                            gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t286, t287, t288, t289, t290);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb34:;
                    }
gm_lb33:;
                    {
                        void * t291 = gm_l0;
                        gm_tl874t2atnlh3 t292 = gm_l1;
                        void * t293 = gm_l0;
                        void * t294 = GM_LD(void *, (((uint8_t *)t293) + 8));
                        gm_t88rcucg1b7uk t295 = gm_l2;
                        gm_b16_8 t296 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t294, t295);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t297;
                        GM_ST(gm_b16_8, ((uint8_t *)&t297 + 0), t296);
                        gm_f_format_vqmfrgb((void *)&gm_l39, (void *)NULL, cx, NULL, t297);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                        if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srcj10g653a00lt, 240, 34);
                        gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                        gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                        gm_t76123gfet73g t301;
                        gm_f_trap_qh84l9d((void *)&t301, (void *)NULL, cx, NULL, t291, t292, t300);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t301);
                        goto gm_fdone;
                    }
                }
gm_lb30:;
            }
            {
                {
                    void * t302 = gm_l0;
                    gm_tl874t2atnlh3 t303 = gm_l1;
                    void * t304 = gm_l0;
                    void * t305 = GM_LD(void *, (((uint8_t *)t304) + 8));
                    gm_t88rcucg1b7uk t306 = gm_l2;
                    gm_b16_8 t307 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t305, t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t308;
                    GM_ST(gm_b16_8, ((uint8_t *)&t308 + 0), t307);
                    gm_f_format_vqmfrgb((void *)&gm_l40, (void *)NULL, cx, NULL, t308);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t309 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                    if (!t309) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcj10g653a00lt, 242, 35);
                    gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                    gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
                    gm_t76123gfet73g t312;
                    gm_f_trap_qh84l9d((void *)&t312, (void *)NULL, cx, NULL, t302, t303, t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8), t312);
                    goto gm_fdone;
                }
gm_lb35:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 202, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_sym_6olbepb F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sajhum9b4nr96));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s6s96daqmhcs9));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)3ULL));
            if (!t27) {
                goto gm_lb5;
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
            gm_l1 = t28;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)4ULL));
            if (!t30) {
                goto gm_lb6;
            }
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
            gm_l1 = t31;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)5ULL));
            if (!t33) {
                goto gm_lb7;
            }
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_siamquc19uikr));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
            gm_l1 = t34;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)6ULL));
            if (!t36) {
                goto gm_lb8;
            }
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s9hi985r6f61m));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
            gm_l1 = t37;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)7ULL));
            if (!t39) {
                goto gm_lb9;
            }
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_smerqm8g793l6));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
            gm_l1 = t40;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)8ULL));
            if (!t42) {
                goto gm_lb10;
            }
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
            gm_l1 = t43;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)9ULL));
            if (!t45) {
                goto gm_lb11;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
            gm_l1 = t46;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)10ULL));
            if (!t48) {
                goto gm_lb12;
            }
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
            gm_l1 = t49;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)11ULL));
            if (!t51) {
                goto gm_lb13;
            }
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s4g3cajt15dmi));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
            gm_l1 = t52;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)12ULL));
            if (!t54) {
                goto gm_lb14;
            }
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
            gm_l1 = t55;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)13ULL));
            if (!t57) {
                goto gm_lb15;
            }
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s02ru83e3fffc));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
            gm_l1 = t58;
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 9, 42);
    }
gm_lb1:;
    gm_b16_8 t59 = gm_l1;
    F.ret = t59;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_seq_of_ifahtjs F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint64_t gm_l4; /* n */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* esz */
    gm_t2lqu8jsj511d gm_l8; /* sp */
    uint64_t gm_l9; /* total */
    gm_b16_8 gm_l11; /* data */
    GM_STACK_CHECK(&F, 961u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l12;
    gm_t934k1j7u70ct gm_l13;
    gm_tabb710d9mq2e gm_l14;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l2;
    gm_f_kind_htv6093((void *)&gm_l12, t8, GM_CONV(gm_csguankko4lmhd, t9));
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)11ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            gm_l4 = t12;
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l12) + 8) + 8));
            gm_l5 = t13;
            {
                gm_t88rcucg1b7uk t14 = gm_l5;
                void * t15 = gm_l0;
                gm_t88rcucg1b7uk t16 = gm_l5;
                uint64_t t17 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t18 = gm_f_us_cuv01fp(t17);
                uint64_t t19 = gm_l4;
                gm_b16_8 t20 = gm_l3;
                gm_t2jeg3bg7sfqa t21;
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21 + 0), t14);
                GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t18);
                GM_ST(uint64_t, ((uint8_t *)&t21 + 16), t19);
                GM_ST(gm_b16_8, ((uint8_t *)&t21 + 24), t20);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
                GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t21);
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)10ULL));
            if (!t23) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l12) + 8) + 4));
            gm_l6 = t24;
            void * t25 = gm_l0;
            gm_t88rcucg1b7uk t26 = gm_l6;
            uint64_t t27 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t27;
            void * t28 = gm_l0;
            gm_b16_8 t29 = gm_l3;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_t2lqu8jsj511d t31 = gm_f_slice_parts_1se5hu4(t28, t30);
            gm_l8 = t31;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint64_t t33 = gm_l7;
            uint64_t r34; bool o35 = __builtin_mul_overflow(t32, t33, &r34);
            uint64_t t36 = r34;
            gm_l9 = t36;
            {
                {
                    uint64_t t37 = gm_l7;
                    uint8_t t38 = (!(t37 == ((uint64_t)0ULL)));
                    if (!t38) {
                        goto gm_lb5;
                    }
                    uint64_t t39 = gm_l9;
                    uint64_t t40 = gm_l7;
                    if (t40 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 191, 33);
                    uint64_t t41 = (uint64_t)(t39 / t40);
                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t43 = (!(t41 == t42));
                    if (!t43) {
                        goto gm_lb5;
                    }
                    {
                        void * t44 = gm_l0;
                        gm_tl874t2atnlh3 t45 = gm_l1;
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sqp26euqk4rkd));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(15));
                        gm_t76123gfet73g t47;
                        gm_f_trap_qh84l9d((void *)&t47, (void *)NULL, cx, NULL, t44, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
                        ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t47);
                        goto gm_fdone;
                    }
                }
gm_lb5:;
            }
gm_lb4:;
            uint64_t t48 = gm_l9;
            uint8_t t49 = ((t48 == ((uint64_t)0ULL)));
            if (t49) {
                gm_t88rcucg1b7uk t50 = gm_l6;
                uint64_t t51 = gm_l7;
                uint64_t t52 = gm_f_us_cuv01fp(t51);
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                gm_b16_8 t54 = gm_f_empty_slice_ijtp9lo();
                gm_t2jeg3bg7sfqa t55;
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55 + 0), t50);
                GM_ST(uint64_t, ((uint8_t *)&t55 + 8), t52);
                GM_ST(uint64_t, ((uint8_t *)&t55 + 16), t53);
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 24), t54);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
                GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t55);
                goto gm_fdone;
            }
            {
                void * t56 = gm_l0;
                void * t57 = (void *)(((uint8_t *)t56) + 184);
                uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                uint64_t t59 = gm_l9;
                gm_f_read_u6r4vns((void *)&gm_l13, t57, t58, t59, ((uint64_t)1ULL));
            }
            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (t60) {
                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_l11 = t61;
            } else {
                {
                    memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l13) + 8), 40);
                }
                {
                    void * t62 = gm_l0;
                    gm_tl874t2atnlh3 t63 = gm_l1;
                    gm_t76123gfet73g t64;
                    gm_f_mem_trap_ecogko0((void *)&t64, (void *)NULL, cx, NULL, t62, t63, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l10)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
                    ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t64);
                    goto gm_fdone;
                }
            }
            {
                gm_t88rcucg1b7uk t65 = gm_l6;
                uint64_t t66 = gm_l7;
                uint64_t t67 = gm_f_us_cuv01fp(t66);
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                gm_b16_8 t69 = gm_l11;
                gm_t2jeg3bg7sfqa t70;
                GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t70 + 0), t65);
                GM_ST(uint64_t, ((uint8_t *)&t70 + 8), t67);
                GM_ST(uint64_t, ((uint8_t *)&t70 + 16), t68);
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 24), t69);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
                GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t70);
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            void * t71 = gm_l0;
            gm_tl874t2atnlh3 t72 = gm_l1;
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 8));
            gm_t88rcucg1b7uk t75 = gm_l2;
            gm_b16_8 t76 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t77;
            GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
            gm_f_format_b946o7j((void *)&gm_l14, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcj10g653a00lt, 196, 35);
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_t76123gfet73g t81;
            gm_f_trap_qh84l9d((void *)&t81, (void *)NULL, cx, NULL, t71, t72, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8), t81);
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 185, 5);
    }
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
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_unop_qamfooj F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* dst */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_ttovhfee8mf28 gm_l12; /* info */
    unsigned __int128 gm_l13; /* v */
    __int128 gm_l14; /* x */
    uint64_t gm_l15; /* n */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* lane */
    gm_b16_8 gm_l23; /* ds */
    gm_t88rcucg1b7uk gm_l24; /* u */
    gm_ttovhfee8mf28 gm_l25; /* info */
    GM_STACK_CHECK(&F, 2614u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l26;
    gm_t53at80pfdus3 gm_l27;
    gm_t53at80pfdus3 gm_l28;
    gm_tcfa6l7reoosn gm_l29;
    gm_tg5lql6itufk7 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tcfa6l7reoosn gm_l32;
    gm_tcfa6l7reoosn gm_l33;
    gm_t53at80pfdus3 gm_l34;
    uint8_t gm_l35;
    gm_tcfa6l7reoosn gm_l36;
    gm_tkn42u7na3t81 gm_l37;
    gm_tg5lql6itufk7 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    {
        {
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
                    if (!t23) {
                        goto gm_lb2;
                    }
                    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t25, gm_srcj10g653a00lt, 86, 19);
                    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t27, gm_srcj10g653a00lt, 86, 31);
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t29 = ((t28 == ((uint8_t)0ULL)));
                    if (t29) {
                        gm_l26 = ((uint8_t)1ULL);
                    } else {
                        gm_l26 = ((uint8_t)0ULL);
                    }
                    uint8_t t30 = gm_l26;
                    GM_ST(uint8_t, (((uint8_t *)t24) + (uintptr_t)((uint64_t)0ULL) * 1), t30);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
                    if (!t32) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                void * t33 = gm_l0;
                                gm_t88rcucg1b7uk t34 = gm_l3;
                                gm_f_kind_htv6093((void *)&gm_l27, t33, GM_CONV(gm_csguankko4lmhd, t34));
                            }
                            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)3ULL));
                            if (!t36) {
                                goto gm_lb5;
                            }
                            void * t37 = gm_l0;
                            gm_t88rcucg1b7uk t38 = gm_l3;
                            void * t39 = gm_l0;
                            gm_t88rcucg1b7uk t40 = gm_l3;
                            gm_b16_8 t41 = gm_l4;
                            double t42 = gm_f_get_float_grmhlm4(t39, GM_CONV(gm_csguankko4lmhd, t40), t41);
                            double t43 = -t42;
                            gm_b16_8 t44 = gm_l5;
                            gm_f_put_float_fiad72d(t37, GM_CONV(gm_csguankko4lmhd, t38), t43, t44);
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                        }
gm_lb5:;
                    }
gm_lb4:;
                    {
                        {
                            {
                                void * t45 = gm_l0;
                                gm_t88rcucg1b7uk t46 = gm_l3;
                                gm_f_kind_htv6093((void *)&gm_l28, t45, GM_CONV(gm_csguankko4lmhd, t46));
                            }
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)11ULL));
                            if (!t48) {
                                goto gm_lb7;
                            }
                            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                            gm_l6 = t49;
                            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l28) + 8) + 8));
                            gm_l7 = t50;
                            void * t51 = gm_l0;
                            gm_t88rcucg1b7uk t52 = gm_l7;
                            uint64_t t53 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t51, t52);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t54 = gm_f_us_cuv01fp(t53);
                            gm_l8 = t54;
                            gm_l10 = ((uint64_t)0ULL);
                            uint64_t t55 = gm_l6;
                            uint64_t t56 = gm_f_us_cuv01fp(t55);
                            gm_l9 = t56;
                            {
                                for (;;) {
                                    uint64_t t57 = gm_l10;
                                    uint64_t t58 = gm_l9;
                                    uint8_t t59 = (t57 < t58);
                                    if (!t59) {
                                        goto gm_lb8;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t60 = gm_l10;
                                                gm_l11 = t60;
                                                void * t61 = gm_l0;
                                                gm_tl874t2atnlh3 t62 = gm_l1;
                                                gm_t9qkh0rsooc8d t63 = gm_l2;
                                                gm_t88rcucg1b7uk t64 = gm_l7;
                                                void * t65 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t67 = gm_l11;
                                                uint64_t t68 = gm_l8;
                                                uint64_t r69; bool o70 = __builtin_mul_overflow(t67, t68, &r69);
                                                if (o70) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 94, 66);
                                                uint64_t t71 = r69;
                                                uint64_t t72 = gm_l11;
                                                uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                                if (o74) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 94, 76);
                                                uint64_t t75 = r73;
                                                uint64_t t76 = gm_l8;
                                                uint64_t r77; bool o78 = __builtin_mul_overflow(t75, t76, &r77);
                                                if (o78) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 94, 75);
                                                uint64_t t79 = r77;
                                                uint64_t t80 = (uintptr_t)t71;
                                                uint64_t t81 = (uintptr_t)t79;
                                                if (t80 > t81 || t81 > (uintptr_t)t66) gm_trap_slice(t80, t81, t66, gm_srcj10g653a00lt, 94, 64);
                                                uint64_t t82 = ((uintptr_t)t79) - (uintptr_t)t71;
                                                gm_b16_8 t83;
                                                GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t65 + (uintptr_t)t71 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(t82));
                                                void * t84 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                uint64_t t86 = gm_l11;
                                                uint64_t t87 = gm_l8;
                                                uint64_t r88; bool o89 = __builtin_mul_overflow(t86, t87, &r88);
                                                if (o89) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 94, 95);
                                                uint64_t t90 = r88;
                                                uint64_t t91 = gm_l11;
                                                uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                                                if (o93) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 94, 105);
                                                uint64_t t94 = r92;
                                                uint64_t t95 = gm_l8;
                                                uint64_t r96; bool o97 = __builtin_mul_overflow(t94, t95, &r96);
                                                if (o97) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 94, 104);
                                                uint64_t t98 = r96;
                                                uint64_t t99 = (uintptr_t)t90;
                                                uint64_t t100 = (uintptr_t)t98;
                                                if (t99 > t100 || t100 > (uintptr_t)t85) gm_trap_slice(t99, t100, t85, gm_srcj10g653a00lt, 94, 91);
                                                uint64_t t101 = ((uintptr_t)t98) - (uintptr_t)t90;
                                                gm_b16_8 t102;
                                                GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t84 + (uintptr_t)t90 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(t101));
                                                gm_f_unop_qamfooj((void *)&gm_l29, (void *)NULL, cx, NULL, t61, t62, t63, t64, t83, t102);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                                if (t103) {
                                                } else {
                                                    gm_t76123gfet73g t104 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l29) + 8));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t104);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb11;
                                            }
gm_lb12:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 94, 17);
                                        }
gm_lb11:;
                                    }
gm_lb10:;
                                    uint64_t t105 = gm_l10;
                                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                                    uint64_t t108 = r106;
                                    gm_l10 = t108;
                                }
                            }
gm_lb8:;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                        }
gm_lb7:;
                    }
gm_lb6:;
                    void * t109 = gm_l0;
                    gm_t88rcucg1b7uk t110 = gm_l3;
                    gm_tg5lql6itufk7 t111 = gm_f_int_info_ulcevdv(t109, GM_CONV(gm_csguankko4lmhd, t110));
                    gm_l30 = t111;
                    uint8_t t112 = (uint8_t)((((uint8_t *)&gm_l30))[8] != 0);
                    if (t112) {
                        gm_ttovhfee8mf28 t113 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l30));
                        gm_l12 = t113;
                    } else {
                        void * t114 = gm_l0;
                        gm_tl874t2atnlh3 t115 = gm_l1;
                        void * t116 = gm_l0;
                        void * t117 = GM_LD(void *, (((uint8_t *)t116) + 8));
                        gm_t88rcucg1b7uk t118 = gm_l3;
                        gm_b16_8 t119 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t117, t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t120;
                        GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
                        gm_f_format_vno937p((void *)&gm_l31, (void *)NULL, cx, NULL, t120);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcj10g653a00lt, 97, 61);
                        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                        gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                        gm_t76123gfet73g t124;
                        gm_f_trap_qh84l9d((void *)&t124, (void *)NULL, cx, NULL, t114, t115, t123);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t124);
                        goto gm_fdone;
                    }
                    void * t125 = gm_l0;
                    gm_t88rcucg1b7uk t126 = gm_l3;
                    gm_b16_8 t127 = gm_l4;
                    unsigned __int128 t128 = gm_f_get_int_pg7p4gc(t125, GM_CONV(gm_csguankko4lmhd, t126), t127);
                    gm_l13 = t128;
                    uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)&gm_l12));
                    if (t129) {
                        unsigned __int128 t130 = gm_l13;
                        __int128 t131;
                        memset(&t131, 0, sizeof t131); memcpy(&t131, ((uint8_t *)&t130), sizeof t131 < 16 ? sizeof t131 : 16);
                        gm_l14 = t131;
                        __int128 t132 = gm_l14;
                        uint32_t t133 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                        __int128 t134 = gm_f_int_lo_i8jbm6e(t133);
                        uint8_t t135 = ((t132 == t134));
                        if (t135) {
                            {
                                void * t136 = gm_l0;
                                gm_tl874t2atnlh3 t137 = gm_l1;
                                gm_b16_8 t138;
                                GM_ST(void *, (uint8_t *)&t138, (void *)(gm_skolmnfc0ccbd));
                                GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(1));
                                gm_f_checked_overflow_ma1bm1g((void *)&gm_l32, (void *)NULL, cx, NULL, t136, t137, t138);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (t139) {
                            } else {
                                gm_t76123gfet73g t140 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l32) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t140);
                                goto gm_fdone;
                            }
                            void * t141 = gm_l0;
                            gm_b16_8 t142 = gm_l5;
                            unsigned __int128 t143 = gm_l13;
                            gm_f_put_uint_cddin99(t141, t142, t143);
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                        }
                        void * t144 = gm_l0;
                        gm_b16_8 t145 = gm_l5;
                        __int128 t146 = gm_l14;
                        if (t146 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcj10g653a00lt, 106, 34);
                        __int128 t147 = (__int128)((unsigned __int128)0 - (unsigned __int128)t146);
                        unsigned __int128 t148;
                        memset(&t148, 0, sizeof t148); memcpy(&t148, ((uint8_t *)&t147), sizeof t148 < 16 ? sizeof t148 : 16);
                        gm_f_put_uint_cddin99(t144, t145, t148);
                    } else {
                        unsigned __int128 t149 = gm_l13;
                        uint8_t t150 = ((t149 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                        if (t150) {
                            gm_b16_8 t151 = gm_l5;
                            gm_f_fill_abmf5g5(t151, ((uint8_t)0ULL));
                        } else {
                            void * t152 = gm_l0;
                            gm_tl874t2atnlh3 t153 = gm_l1;
                            gm_b16_8 t154;
                            GM_ST(void *, (uint8_t *)&t154, (void *)(gm_skolmnfc0ccbd));
                            GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(1));
                            gm_f_checked_overflow_ma1bm1g((void *)&gm_l33, (void *)NULL, cx, NULL, t152, t153, t154);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                            if (t155) {
                            } else {
                                gm_t76123gfet73g t156 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l33) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t156);
                                goto gm_fdone;
                            }
                            void * t157 = gm_l0;
                            gm_b16_8 t158 = gm_l5;
                            uint32_t t159 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                            unsigned __int128 t160 = gm_l13;
                            unsigned __int128 r161; bool o162 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t160, &r161);
                            unsigned __int128 t163 = r161;
                            unsigned __int128 t164 = gm_f_num_ext_348kmbu(((uint8_t)0), t159, t163);
                            gm_f_put_uint_cddin99(t157, t158, t164);
                        }
                    }
                    goto gm_lb1;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)1ULL));
                    if (!t166) {
                        goto gm_lb13;
                    }
                    {
                        {
                            {
                                void * t167 = gm_l0;
                                gm_t88rcucg1b7uk t168 = gm_l3;
                                gm_f_kind_htv6093((void *)&gm_l34, t167, GM_CONV(gm_csguankko4lmhd, t168));
                            }
                            uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l34) + 0));
                            uint8_t t170 = (uint8_t)(t169 == ((uint8_t)11ULL));
                            if (!t170) {
                                goto gm_lb15;
                            }
                            uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                            gm_l15 = t171;
                            gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l34) + 8) + 8));
                            gm_l16 = t172;
                            void * t173 = gm_l0;
                            gm_t88rcucg1b7uk t174 = gm_l16;
                            uint64_t t175 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t173, t174);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t176 = gm_f_us_cuv01fp(t175);
                            gm_l17 = t176;
                            {
                                void * t177 = gm_l0;
                                gm_t88rcucg1b7uk t178 = gm_l16;
                                gm_t53at80pfdus3 t179;
                                gm_f_kind_htv6093((void *)&t179, t177, GM_CONV(gm_csguankko4lmhd, t178));
                                uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
                                uint8_t t181 = (uint8_t)((t180 == ((uint8_t)4ULL)));
                                gm_t53at80pfdus3 t182;
                                memset(&t182, 0, sizeof t182);
                                GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)4ULL));
                                gm_l18 = t181;
                            }
                            gm_l20 = ((uint64_t)0ULL);
                            uint64_t t183 = gm_l15;
                            uint64_t t184 = gm_f_us_cuv01fp(t183);
                            gm_l19 = t184;
                            {
                                for (;;) {
                                    uint64_t t185 = gm_l20;
                                    uint64_t t186 = gm_l19;
                                    uint8_t t187 = (t185 < t186);
                                    if (!t187) {
                                        goto gm_lb16;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t188 = gm_l20;
                                                gm_l21 = t188;
                                                void * t189 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t191 = gm_l21;
                                                uint64_t t192 = gm_l17;
                                                uint64_t r193; bool o194 = __builtin_mul_overflow(t191, t192, &r193);
                                                if (o194) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 119, 31);
                                                uint64_t t195 = r193;
                                                uint64_t t196 = gm_l21;
                                                uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                                                if (o198) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 119, 41);
                                                uint64_t t199 = r197;
                                                uint64_t t200 = gm_l17;
                                                uint64_t r201; bool o202 = __builtin_mul_overflow(t199, t200, &r201);
                                                if (o202) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 119, 40);
                                                uint64_t t203 = r201;
                                                uint64_t t204 = (uintptr_t)t195;
                                                uint64_t t205 = (uintptr_t)t203;
                                                if (t204 > t205 || t205 > (uintptr_t)t190) gm_trap_slice(t204, t205, t190, gm_srcj10g653a00lt, 119, 29);
                                                uint64_t t206 = ((uintptr_t)t203) - (uintptr_t)t195;
                                                gm_b16_8 t207;
                                                GM_ST(void *, (uint8_t *)&t207, (void *)(((uint8_t *)t189 + (uintptr_t)t195 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(t206));
                                                gm_l22 = t207;
                                                void * t208 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                uint64_t t210 = gm_l21;
                                                uint64_t t211 = gm_l17;
                                                uint64_t r212; bool o213 = __builtin_mul_overflow(t210, t211, &r212);
                                                if (o213) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 120, 31);
                                                uint64_t t214 = r212;
                                                uint64_t t215 = gm_l21;
                                                uint64_t r216; bool o217 = __builtin_add_overflow(t215, ((uint64_t)1ULL), &r216);
                                                if (o217) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 120, 41);
                                                uint64_t t218 = r216;
                                                uint64_t t219 = gm_l17;
                                                uint64_t r220; bool o221 = __builtin_mul_overflow(t218, t219, &r220);
                                                if (o221) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 120, 40);
                                                uint64_t t222 = r220;
                                                uint64_t t223 = (uintptr_t)t214;
                                                uint64_t t224 = (uintptr_t)t222;
                                                if (t223 > t224 || t224 > (uintptr_t)t209) gm_trap_slice(t223, t224, t209, gm_srcj10g653a00lt, 120, 27);
                                                uint64_t t225 = ((uintptr_t)t222) - (uintptr_t)t214;
                                                gm_b16_8 t226;
                                                GM_ST(void *, (uint8_t *)&t226, (void *)(((uint8_t *)t208 + (uintptr_t)t214 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(t225));
                                                gm_l23 = t226;
                                                uint8_t t227 = gm_l18;
                                                if (t227) {
                                                    void * t228 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t229)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t229, gm_srcj10g653a00lt, 122, 25);
                                                    void * t230 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                    uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t231)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t231, gm_srcj10g653a00lt, 122, 36);
                                                    uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)t230) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                    uint8_t t233 = ((t232 == ((uint8_t)0ULL)));
                                                    if (t233) {
                                                        gm_l35 = ((uint8_t)1ULL);
                                                    } else {
                                                        gm_l35 = ((uint8_t)0ULL);
                                                    }
                                                    uint8_t t234 = gm_l35;
                                                    GM_ST(uint8_t, (((uint8_t *)t228) + (uintptr_t)((uint64_t)0ULL) * 1), t234);
                                                } else {
                                                    void * t235 = gm_l0;
                                                    gm_tl874t2atnlh3 t236 = gm_l1;
                                                    gm_t9qkh0rsooc8d t237 = gm_l2;
                                                    gm_t88rcucg1b7uk t238 = gm_l16;
                                                    gm_b16_8 t239 = gm_l22;
                                                    gm_b16_8 t240 = gm_l23;
                                                    gm_f_unop_qamfooj((void *)&gm_l36, (void *)NULL, cx, NULL, t235, t236, t237, t238, t239, t240);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t241 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                    if (t241) {
                                                    } else {
                                                        gm_t76123gfet73g t242 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l36) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t242);
                                                        goto gm_fdone;
                                                    }
                                                }
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 118, 17);
                                        }
gm_lb19:;
                                    }
gm_lb18:;
                                    uint64_t t243 = gm_l20;
                                    uint64_t r244; bool o245 = __builtin_add_overflow(t243, ((uint64_t)1ULL), &r244);
                                    uint64_t t246 = r244;
                                    gm_l20 = t246;
                                }
                            }
gm_lb16:;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                        }
gm_lb15:;
                    }
gm_lb14:;
                    void * t247 = gm_l0;
                    gm_t88rcucg1b7uk t248 = gm_l3;
                    gm_tkn42u7na3t81 t249 = gm_f_bits_struct_ty_22ue20s(t247, GM_CONV(gm_csguankko4lmhd, t248));
                    gm_l37 = t249;
                    uint8_t t250 = (uint8_t)((((uint8_t *)&gm_l37))[4] != 0);
                    if (t250) {
                        gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l37));
                        gm_l24 = t251;
                    } else {
                        gm_t88rcucg1b7uk t252 = gm_l3;
                        gm_l24 = t252;
                    }
                    void * t253 = gm_l0;
                    gm_t88rcucg1b7uk t254 = gm_l24;
                    gm_tg5lql6itufk7 t255 = gm_f_int_info_ulcevdv(t253, GM_CONV(gm_csguankko4lmhd, t254));
                    gm_l38 = t255;
                    uint8_t t256 = (uint8_t)((((uint8_t *)&gm_l38))[8] != 0);
                    if (t256) {
                        gm_ttovhfee8mf28 t257 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l38));
                        gm_l25 = t257;
                    } else {
                        void * t258 = gm_l0;
                        gm_tl874t2atnlh3 t259 = gm_l1;
                        void * t260 = gm_l0;
                        void * t261 = GM_LD(void *, (((uint8_t *)t260) + 8));
                        gm_t88rcucg1b7uk t262 = gm_l24;
                        gm_b16_8 t263 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t261, t262);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t264;
                        GM_ST(gm_b16_8, ((uint8_t *)&t264 + 0), t263);
                        gm_f_format_gd3p27d((void *)&gm_l39, (void *)NULL, cx, NULL, t264);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t265 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                        if (!t265) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srcj10g653a00lt, 130, 61);
                        gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                        gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&t266));
                        gm_t76123gfet73g t268;
                        gm_f_trap_qh84l9d((void *)&t268, (void *)NULL, cx, NULL, t258, t259, t267);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t268);
                        goto gm_fdone;
                    }
                    void * t269 = gm_l0;
                    gm_b16_8 t270 = gm_l5;
                    uint8_t t271 = GM_LD(uint8_t, ((uint8_t *)&gm_l25));
                    uint32_t t272 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                    void * t273 = gm_l0;
                    gm_t88rcucg1b7uk t274 = gm_l24;
                    gm_b16_8 t275 = gm_l4;
                    unsigned __int128 t276 = gm_f_get_int_pg7p4gc(t273, GM_CONV(gm_csguankko4lmhd, t274), t275);
                    unsigned __int128 t277 = (unsigned __int128)~t276;
                    unsigned __int128 t278 = gm_f_num_ext_348kmbu(t271, t272, t277);
                    gm_f_put_uint_cddin99(t269, t270, t278);
                    goto gm_lb1;
                }
gm_lb13:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 85, 5);
            }
        }
gm_lb1:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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

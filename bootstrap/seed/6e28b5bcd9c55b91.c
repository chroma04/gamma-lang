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
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t22b1ebhvur5a;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_b16_8 f2; } gm_t8qqrlhb610a2;
typedef struct { gm_tk9n3vlila9qg f0; uint8_t f1; uint8_t pe[7]; } gm_ta2tdjm589013;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_t0uuhaljsf410 f0; uint8_t f1; uint8_t pe[7]; } gm_ti6pdj2sunpe4;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_th2lqbo77pvsc f2; } gm_tmf08dt0mom0q;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_tk9n3vlila9qg f1; } gm_toa5kh9gm9738;
typedef struct { gm_tu1089hhijadf f0; uint8_t f1; uint8_t pe[7]; } gm_tom1ad5bcqgi1;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t0uuhaljsf410 v1; } f1; } gm_tq6bq450l0idg;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_th2lqbo77pvsc f6; gm_b16_8 f7; gm_b16_8 f8; } gm_ts1nru0t7g7aj;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
typedef struct { gm_tin97tt4ud0s1 f0; gm_tin97tt4ud0s1 f1; } gm_t08dlk5fmn1l9;
_Static_assert(sizeof(gm_t22b1ebhvur5a) == 40 && _Alignof(gm_t22b1ebhvur5a) == 8 && offsetof(gm_t22b1ebhvur5a, f0) == 0 && offsetof(gm_t22b1ebhvur5a, f1) == 8, "layout of `string!(ident: string, rest: string)`");
typedef struct { gm_b16_8 f0; gm_tmf08dt0mom0q f1; } gm_t3rj5o15e922s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
_Static_assert(sizeof(gm_t8qqrlhb610a2) == 56 && _Alignof(gm_t8qqrlhb610a2) == 8 && offsetof(gm_t8qqrlhb610a2, f0) == 0 && offsetof(gm_t8qqrlhb610a2, f1) == 16 && offsetof(gm_t8qqrlhb610a2, f2) == 40, "layout of `Choice`");
_Static_assert(sizeof(gm_ta2tdjm589013) == 48 && _Alignof(gm_ta2tdjm589013) == 8 && offsetof(gm_ta2tdjm589013, f0) == 0 && offsetof(gm_ta2tdjm589013, f1) == 40, "layout of `?StackItem`");
typedef struct { gm_t6ok5tvcjc200 f0; gm_b16_8 f1; } gm_tbqru6ibafrpr;
typedef struct { gm_tmf08dt0mom0q f0; uint8_t f1; uint8_t pe[7]; } gm_tcmp1j2cnqi8i;
typedef struct { gm_t8qqrlhb610a2 f0; uint8_t f1; uint8_t pe[7]; } gm_tgi1k2malmpgi;
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_ti6pdj2sunpe4) == 72 && _Alignof(gm_ti6pdj2sunpe4) == 8 && offsetof(gm_ti6pdj2sunpe4, f0) == 0 && offsetof(gm_ti6pdj2sunpe4, f1) == 64, "layout of `?TemplateArgs`");
typedef struct { gm_ts1nru0t7g7aj f0; uint8_t f1; uint8_t pe[7]; } gm_tie69pa8pplgc;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tmf08dt0mom0q) == 104 && _Alignof(gm_tmf08dt0mom0q) == 8 && offsetof(gm_tmf08dt0mom0q, f0) == 0 && offsetof(gm_tmf08dt0mom0q, f1) == 40 && offsetof(gm_tmf08dt0mom0q, f2) == 80, "layout of `Refs`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t8qqrlhb610a2 v1; } f1; } gm_tmfqp43enl3pa;
typedef struct { gm_t6ok5tvcjc200 f0; gm_trqfvmjtt4sij f1; } gm_to54va2pes5v2;
_Static_assert(sizeof(gm_toa5kh9gm9738) == 48 && _Alignof(gm_toa5kh9gm9738) == 8 && offsetof(gm_toa5kh9gm9738, f0) == 0 && offsetof(gm_toa5kh9gm9738, f1) == 8, "layout of `(i: usize, x: LintRule)`");
_Static_assert(sizeof(gm_tom1ad5bcqgi1) == 64 && _Alignof(gm_tom1ad5bcqgi1) == 8 && offsetof(gm_tom1ad5bcqgi1, f0) == 0 && offsetof(gm_tom1ad5bcqgi1, f1) == 56, "layout of `?Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tq6bq450l0idg) == 72 && _Alignof(gm_tq6bq450l0idg) == 8 && offsetof(gm_tq6bq450l0idg, f0) == 0 && offsetof(gm_tq6bq450l0idg, f1) == 8, "layout of `string!ArchiveInfo`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ts1nru0t7g7aj) == 168 && _Alignof(gm_ts1nru0t7g7aj) == 8 && offsetof(gm_ts1nru0t7g7aj, f0) == 0 && offsetof(gm_ts1nru0t7g7aj, f1) == 16 && offsetof(gm_ts1nru0t7g7aj, f2) == 32 && offsetof(gm_ts1nru0t7g7aj, f3) == 56 && offsetof(gm_ts1nru0t7g7aj, f4) == 80 && offsetof(gm_ts1nru0t7g7aj, f5) == 96 && offsetof(gm_ts1nru0t7g7aj, f6) == 112 && offsetof(gm_ts1nru0t7g7aj, f7) == 136 && offsetof(gm_ts1nru0t7g7aj, f8) == 152, "layout of `LockedPackage`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
typedef struct { gm_t6ok5tvcjc200 f0; gm_t8qqrlhb610a2 f1; } gm_tv2l6796tk7ol;
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tmf08dt0mom0q v1; } f1; } gm_tv61mo81rn7eb;
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t08dlk5fmn1l9) == 80 && _Alignof(gm_t08dlk5fmn1l9) == 8 && offsetof(gm_t08dlk5fmn1l9, f0) == 0 && offsetof(gm_t08dlk5fmn1l9, f1) == 40, "layout of `(res: Resolution, lock: Lock)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_toq2f2q4opoa3 f3; } gm_t2f6hp2vik01f;
_Static_assert(sizeof(gm_t3rj5o15e922s) == 120 && _Alignof(gm_t3rj5o15e922s) == 8 && offsetof(gm_t3rj5o15e922s, f0) == 0 && offsetof(gm_t3rj5o15e922s, f1) == 16, "layout of `UrlRefs`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_tbqru6ibafrpr) == 56 && _Alignof(gm_tbqru6ibafrpr) == 8 && offsetof(gm_tbqru6ibafrpr, f0) == 0 && offsetof(gm_tbqru6ibafrpr, f1) == 40, "layout of `Owner`");
_Static_assert(sizeof(gm_tcmp1j2cnqi8i) == 112 && _Alignof(gm_tcmp1j2cnqi8i) == 8 && offsetof(gm_tcmp1j2cnqi8i, f0) == 0 && offsetof(gm_tcmp1j2cnqi8i, f1) == 104, "layout of `?Refs`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
_Static_assert(sizeof(gm_tgi1k2malmpgi) == 64 && _Alignof(gm_tgi1k2malmpgi) == 8 && offsetof(gm_tgi1k2malmpgi, f0) == 0 && offsetof(gm_tgi1k2malmpgi, f1) == 56, "layout of `?Choice`");
_Static_assert(sizeof(gm_tie69pa8pplgc) == 176 && _Alignof(gm_tie69pa8pplgc) == 8 && offsetof(gm_tie69pa8pplgc, f0) == 0 && offsetof(gm_tie69pa8pplgc, f1) == 168, "layout of `?LockedPackage`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tikairc198460;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
typedef struct { gm_b16_8 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[6]; gm_tl572d7579hns f2; gm_tnpifv20irnph f3; gm_tv37d3dpt2opu f4; gm_tin97tt4ud0s1 f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_trqfvmjtt4sij f9; gm_tin97tt4ud0s1 f10; gm_trqfvmjtt4sij f11; gm_tin97tt4ud0s1 f12; gm_tin97tt4ud0s1 f13; gm_tin97tt4ud0s1 f14; gm_tin97tt4ud0s1 f15; uint8_t f16; uint8_t p17[7]; gm_tin97tt4ud0s1 f17; gm_trqfvmjtt4sij f18; } gm_tlhfcpum9vunj;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tmfqp43enl3pa) == 64 && _Alignof(gm_tmfqp43enl3pa) == 8 && offsetof(gm_tmfqp43enl3pa, f0) == 0 && offsetof(gm_tmfqp43enl3pa, f1) == 8, "layout of `string!Choice`");
_Static_assert(sizeof(gm_to54va2pes5v2) == 80 && _Alignof(gm_to54va2pes5v2) == 8 && offsetof(gm_to54va2pes5v2, f0) == 0 && offsetof(gm_to54va2pes5v2, f1) == 40, "layout of `(key: Key, value: List(Key))`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
typedef struct { gm_tbqru6ibafrpr f0; uint8_t f1; uint8_t pe[7]; } gm_tqa5rsrlv432m;
typedef struct { gm_t08dlk5fmn1l9 f0; uint8_t f1; uint8_t pe[7]; } gm_tuu655st500b0;
_Static_assert(sizeof(gm_tv2l6796tk7ol) == 96 && _Alignof(gm_tv2l6796tk7ol) == 8 && offsetof(gm_tv2l6796tk7ol, f0) == 0 && offsetof(gm_tv2l6796tk7ol, f1) == 40, "layout of `(key: Key, value: Choice)`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_tv61mo81rn7eb) == 112 && _Alignof(gm_tv61mo81rn7eb) == 8 && offsetof(gm_tv61mo81rn7eb, f0) == 0 && offsetof(gm_tv61mo81rn7eb, f1) == 8, "layout of `string!Refs`");
_Static_assert(sizeof(gm_t2f6hp2vik01f) == 96 && _Alignof(gm_t2f6hp2vik01f) == 8 && offsetof(gm_t2f6hp2vik01f, f0) == 0 && offsetof(gm_t2f6hp2vik01f, f1) == 16 && offsetof(gm_t2f6hp2vik01f, f2) == 32 && offsetof(gm_t2f6hp2vik01f, f3) == 48, "layout of `Req`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tuu655st500b0 v1; } f1; } gm_t8a9rhgj2oa25;
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tikairc198460) == 328 && _Alignof(gm_tikairc198460) == 8 && offsetof(gm_tikairc198460, f0) == 0 && offsetof(gm_tikairc198460, f1) == 8, "layout of `string!Manifest`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
_Static_assert(sizeof(gm_tlhfcpum9vunj) == 640 && _Alignof(gm_tlhfcpum9vunj) == 8 && offsetof(gm_tlhfcpum9vunj, f0) == 0 && offsetof(gm_tlhfcpum9vunj, f1) == 16 && offsetof(gm_tlhfcpum9vunj, f2) == 24 && offsetof(gm_tlhfcpum9vunj, f3) == 48 && offsetof(gm_tlhfcpum9vunj, f4) == 64 && offsetof(gm_tlhfcpum9vunj, f5) == 112 && offsetof(gm_tlhfcpum9vunj, f6) == 152 && offsetof(gm_tlhfcpum9vunj, f7) == 192 && offsetof(gm_tlhfcpum9vunj, f8) == 232 && offsetof(gm_tlhfcpum9vunj, f9) == 272 && offsetof(gm_tlhfcpum9vunj, f10) == 312 && offsetof(gm_tlhfcpum9vunj, f11) == 352 && offsetof(gm_tlhfcpum9vunj, f12) == 392 && offsetof(gm_tlhfcpum9vunj, f13) == 432 && offsetof(gm_tlhfcpum9vunj, f14) == 472 && offsetof(gm_tlhfcpum9vunj, f15) == 512 && offsetof(gm_tlhfcpum9vunj, f16) == 552 && offsetof(gm_tlhfcpum9vunj, f17) == 560 && offsetof(gm_tlhfcpum9vunj, f18) == 600, "layout of `Resolver`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
_Static_assert(sizeof(gm_tqa5rsrlv432m) == 64 && _Alignof(gm_tqa5rsrlv432m) == 8 && offsetof(gm_tqa5rsrlv432m, f0) == 0 && offsetof(gm_tqa5rsrlv432m, f1) == 56, "layout of `?Owner`");
_Static_assert(sizeof(gm_tuu655st500b0) == 88 && _Alignof(gm_tuu655st500b0) == 8 && offsetof(gm_tuu655st500b0, f0) == 0 && offsetof(gm_tuu655st500b0, f1) == 80, "layout of `?(res: Resolution, lock: Lock)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_t8a9rhgj2oa25) == 96 && _Alignof(gm_t8a9rhgj2oa25) == 8 && offsetof(gm_t8a9rhgj2oa25, f0) == 0 && offsetof(gm_t8a9rhgj2oa25, f1) == 8, "layout of `string!?(res: Resolution, lock: Lock)`");
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_add_req_klu87ju {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l2; /* req */
    gm_trqfvmjtt4sij l3; /* reqs */
    gm_t2f6hp2vik01f l7; /* q */
    gm_b0_1 ret;
};
struct gm_fr_after_req_ti0a6un {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l2; /* req */
    gm_t8qqrlhb610a2 l3; /* c */
    gm_b0_1 ret;
};
struct gm_fr_archive_info_gq6dkee {
    gm_frame h;
    gm_t0uuhaljsf410 l4; /* a */
    gm_ti6pdj2sunpe4 ret;
};
struct gm_fr_build_lock_hmfdkj8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* packages */
    gm_trqfvmjtt4sij l2; /* keys */
    gm_tv2l6796tk7ol l5; /* c */
    gm_t6ok5tvcjc200 l8; /* key */
    gm_t8qqrlhb610a2 l9; /* choice */
    gm_ts1nru0t7g7aj l12; /* l */
    gm_trqfvmjtt4sij l13; /* used */
    gm_t6ok5tvcjc200 l16; /* k */
    gm_t6ok5tvcjc200 l19; /* key */
    gm_t0uuhaljsf410 l21; /* info */
    gm_t62efos6ssciq ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_refresh_names_9ih3ok9 {
    gm_frame h;
    gm_tv2l6796tk7ol l9; /* c */
    gm_to54va2pes5v2 l15; /* c */
    gm_t2f6hp2vik01f l18; /* q */
    gm_tl572d7579hns ret;
};
struct gm_fr_choose_9ee155u {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t8qqrlhb610a2 l2; /* c */
    gm_tie69pa8pplgc l4; /* locked */
    gm_ts1nru0t7g7aj l6; /* l */
    gm_t2f6hp2vik01f l9; /* q */
    gm_t8qqrlhb610a2 l10; /* choice */
    gm_ts1nru0t7g7aj l11; /* l */
    gm_tmf08dt0mom0q l14; /* refs */
    gm_tmfqp43enl3pa ret;
};
struct gm_fr_clear_m4blb4l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_pirklev {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_qd87n65 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_rm1b3rc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_s0loit2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_db_tssgvk0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_dependency_names_6m0jc80 {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l2; /* names */
    gm_trqfvmjtt4sij l3; /* keys */
    gm_t6ok5tvcjc200 l6; /* k */
    gm_trqfvmjtt4sij l7; /* out */
    gm_b16_8 ret;
};
struct gm_fr_describe_mp0u2mq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* parts */
    gm_t2f6hp2vik01f l4; /* r */
    gm_trqfvmjtt4sij l5; /* out */
    gm_b16_8 ret;
};
struct gm_fr_discard_bdpju80 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_downloads_vdenqu5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effective_vikkdqh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* others */
    gm_t2f6hp2vik01f l4; /* r */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_14fhq9i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_attt3ii {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cpdaeui {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gsm9gv5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hr6df4i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kvf8gur {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lcqki20 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mnej5m4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n8lsnom {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oihj9aq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p1sl7gg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pkfv52g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s0g91u7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ensure_archive_70kh53q {
    gm_frame h;
    gm_tt008mgut8glb l2; /* u */
    gm_t0uuhaljsf410 l3; /* info */
    gm_tie69pa8pplgc l6; /* locked */
    gm_tin97tt4ud0s1 l7; /* old */
    gm_ts1nru0t7g7aj l10; /* p */
    gm_ts1nru0t7g7aj l12; /* l */
    gm_ts1nru0t7g7aj l19; /* l */
    gm_t0uuhaljsf410 l23; /* info */
    gm_t0uuhaljsf410 l32; /* info */
    gm_t0uuhaljsf410 l33; /* info */
    gm_tq6bq450l0idg ret;
};
struct gm_fr_ensure_checkout_hvmsso1 {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t8qqrlhb610a2 l2; /* choice */
    gm_t2f6hp2vik01f l5; /* first */
    gm_ts1nru0t7g7aj l13; /* l */
    gm_ts1nru0t7g7aj l14; /* l */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_entry_name_qnlemsm {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l3; /* deps */
    gm_trqfvmjtt4sij l4; /* reqs */
    gm_t2f6hp2vik01f l7; /* q */
    gm_b16_8 ret;
};
struct gm_fr_find_ref_h89e731 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_0c1hioc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2orhd15 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3pk07bs {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7sll6e8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_95e0vi0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9di2rrs {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ajecpf6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_asjuqmr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bpl33sh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cvqjb45 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dvcii4b {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eagbsnc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fj9bgml {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fnh5q7b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g1540jg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ia6bs6h {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jth3mgf {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l821hfj {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lbar0n6 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m5mroph {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mv68p71 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6eprd4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n8h20c1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nsoplg3 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o83d7mn {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ro2n9te {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rpqj2mb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s20j3pl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u75ve7h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vakr6l2 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_default_p4tis8s {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* s */
    uint8_t ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_0k0up67 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_refreshed_ifi7dst {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l3; /* reqs */
    gm_t2f6hp2vik01f l9; /* q */
    gm_ts1nru0t7g7aj l10; /* l */
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_key_equal_n71njnf {
    gm_frame h;
    gm_t6ok5tvcjc200 l0; /* a */
    gm_t6ok5tvcjc200 l1; /* b */
    uint8_t ret;
};
struct gm_fr_key_less_9obq0rq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_map_136uk7q {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_e8nmg3h {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_ihrh7ij {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_kjnah27 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_list_0pjrgaq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1q70459 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_254rpp1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_2hljflu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6vurufm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_agmtpnt {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dc4p80b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jsujn2k {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nccb2fo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o444f8n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pfs7ocg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qeqmcmu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vsi9vun {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_locked_entry_4693mpc {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_tin97tt4ud0s1 l2; /* old */
    gm_tie69pa8pplgc ret;
};
struct gm_fr_make_read_only_a29eok8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_make_writable_6uascrq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mirror_8u3u6oe {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l5; /* first */
    gm_tmf08dt0mom0q l7; /* refs */
    gm_tl572d7579hns ret;
};
struct gm_fr_pick_ronijl9 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* pins */
    gm_t2f6hp2vik01f l8; /* r */
    gm_t2f6hp2vik01f l12; /* p */
    gm_trqfvmjtt4sij l13; /* versions */
    gm_t2f6hp2vik01f l16; /* r */
    gm_tin97tt4ud0s1 l17; /* v */
    gm_t2f6hp2vik01f l19; /* pin */
    gm_tu1089hhijadf l22; /* v */
    gm_tin97tt4ud0s1 l25; /* r */
    gm_tom1ad5bcqgi1 l30; /* best_v */
    gm_tu1089hhijadf l36; /* v */
    gm_tin97tt4ud0s1 l40; /* r */
    gm_tu1089hhijadf l41; /* b */
    gm_tu1089hhijadf l42; /* v */
    gm_tmfqp43enl3pa ret;
};
struct gm_fr_pop_rnfkbb9 {
    gm_frame h;
    gm_tk9n3vlila9qg l2; /* x */
    gm_ta2tdjm589013 ret;
};
struct gm_fr_push_1htqqaf {
    gm_frame h;
    gm_ts1nru0t7g7aj l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7mbittu {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d61dqrj {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_fp6nnvl {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_i535kr1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ljifgtt {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_mt615h0 {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r6o112l {
    gm_frame h;
    gm_t3rj5o15e922s l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vgb8k63 {
    gm_frame h;
    gm_t2f6hp2vik01f l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_manifest_u9uajvn {
    gm_frame h;
    gm_tikairc198460 ret;
};
struct gm_fr_record_dep_84vd3la {
    gm_frame h;
    gm_tqa5rsrlv432m l1; /* owner */
    gm_t6ok5tvcjc200 l2; /* key */
    gm_tbqru6ibafrpr l3; /* o */
    gm_trqfvmjtt4sij l5; /* keys */
    gm_b0_1 ret;
};
struct gm_fr_recorded_version_lb5b4vt {
    gm_frame h;
    gm_t2f6hp2vik01f l7; /* r */
    gm_tu1089hhijadf l9; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_refs_of_jfglrbh {
    gm_frame h;
    gm_t3rj5o15e922s l4; /* it */
    gm_tcmp1j2cnqi8i ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_6apusur {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cr2718s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ibbj0r1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_isbo8va {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_moan1lp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o2oupjk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ptf2p0d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_qn6ph56 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vdf56ao {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remove_tree_rp98g1s {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_req_same_h2pl2bq {
    gm_frame h;
    gm_t2f6hp2vik01f l0; /* a */
    gm_t2f6hp2vik01f l1; /* b */
    uint8_t ret;
};
struct gm_fr_resolver_ms661bf {
    gm_frame h;
    gm_tv37d3dpt2opu l3; /* old */
    gm_tlhfcpum9vunj ret;
};
struct gm_fr_run_resolver_tf2ua9j {
    gm_frame h;
    gm_tm43kqe3e8b4a l2; /* manifest */
    gm_tin97tt4ud0s1 l4; /* lock */
    gm_t8a9rhgj2oa25 ret;
};
struct gm_fr_satisfies_mlodg56 {
    gm_frame h;
    gm_toq2f2q4opoa3 l2; /* spec */
    gm_tin97tt4ud0s1 l6; /* req */
    gm_tu1089hhijadf l8; /* v */
    uint8_t ret;
};
struct gm_fr_sha_mismatch_lq7bp8e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_short_rev_uin3i2s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_1kptd3j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_subdir_of_gjqk3qb {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_summarize_archive_cicsplu {
    gm_frame h;
    gm_tq6bq450l0idg ret;
};
struct gm_fr_tag_version_aloeltn {
    gm_frame h;
    gm_tom1ad5bcqgi1 ret;
};
struct gm_fr_walk_59naf6k {
    gm_frame h;
    gm_tqa5rsrlv432m l4; /* owner */
    gm_tk9n3vlila9qg l8; /* s */
    gm_tk9n3vlila9qg l12; /* t */
    gm_trqfvmjtt4sij l13; /* names */
    gm_tk9n3vlila9qg l16; /* t */
    gm_trqfvmjtt4sij l17; /* deps */
    gm_t7tiv6vif86qg l20; /* dep */
    gm_tbqru6ibafrpr l24; /* o */
    gm_tm43kqe3e8b4a l27; /* m */
    gm_te5jk64pjl28k l28; /* g */
    gm_t6ok5tvcjc200 l29; /* key */
    gm_t8qqrlhb610a2 l30; /* choice */
    gm_tm43kqe3e8b4a l33; /* m */
    gm_tt008mgut8glb l35; /* u */
    gm_t0uuhaljsf410 l36; /* info */
    gm_t6ok5tvcjc200 l37; /* key */
    gm_tm43kqe3e8b4a l40; /* m */
    gm_t6ok5tvcjc200 l44; /* k */
    gm_tl572d7579hns ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_req_klu87ju(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_after_req_ti0a6un(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_archive_dir_srvvp3g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_archive_info_gq6dkee(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ti6pdj2sunpe4 gm_f_archive_info_gq6dkee_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_build_lock_hmfdkj8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_refresh_names_9ih3ok9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_checkout_dir_mvijcuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_checkout_j42th18(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_choose_9ee155u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_clear_m4blb4l(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_pirklev(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_qd87n65(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_s0loit2(void * a0);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dependency_names_6m0jc80(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_mp0u2mq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_drop_nns2kfo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_effective_vikkdqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_attt3ii(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cpdaeui(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gsm9gv5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvf8gur(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lcqki20(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mnej5m4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oihj9aq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p1sl7gg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s0g91u7(void);
__attribute__((aligned(4))) void gm_f_ensure_archive_70kh53q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_ensure_checkout_hvmsso1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_entry_name_qnlemsm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_fetch_mirror_q06gvv0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_find_locked_83oh60j(void *gm_out, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_find_ref_h89e731(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_find_ref_h89e731_c(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_0c1hioc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_2orhd15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3pk07bs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7sll6e8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_95e0vi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9di2rrs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ajecpf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_asjuqmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bpl33sh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dvcii4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eagbsnc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fj9bgml(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_fnh5q7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_g1540jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ia6bs6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jth3mgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_m5mroph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mv68p71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_n8h20c1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nsoplg3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_o83d7mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ro2n9te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rpqj2mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_u75ve7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vakr6l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_get_7q4fb48(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_9fn35sd(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_a0eoncd(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_vc34oj2(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_introsort_h9phkod(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s_c(gm_toq2f2q4opoa3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_refreshed_ifi7dst(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf_c(gm_t6ok5tvcjc200 a0, gm_t6ok5tvcjc200 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_key_map_136uk7q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_e8nmg3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_ihrh7ij(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_kjnah27(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1q70459(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_254rpp1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_2hljflu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6vurufm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_agmtpnt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dc4p80b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jsujn2k(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nccb2fo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qeqmcmu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_refs_j3is1o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_vsi9vun(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_locked_entry_4693mpc_c(void * a0, gm_t6ok5tvcjc200 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_used_5dtkpil(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_mirror_8u3u6oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_pick_ronijl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1htqqaf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7mbittu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d61dqrj(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fp6nnvl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ljifgtt(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mt615h0(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r6o112l(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgb8k63(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_1jc6f83(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_4eaoh8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_jng5s3d(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_ui22st8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_text_g0qbj1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_dep_84vd3la(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_recorded_version_lb5b4vt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_refs_of_jfglrbh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tcmp1j2cnqi8i gm_f_refs_of_jfglrbh_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6apusur(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cr2718s(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ibbj0r1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_isbo8va(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_moan1lp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o2oupjk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ptf2p0d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qn6ph56(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vdf56ao(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_repo_dir_name_6927pg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq_c(gm_t2f6hp2vik01f a0, gm_t2f6hp2vik01f a1);
__attribute__((aligned(4))) void gm_f_resolver_ms661bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_rev_parse_3l092b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_resolver_tf2ua9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_mlodg56(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sha_mismatch_lq7bp8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_slot_5e9u9id(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_slot_9ns6k3q(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_1kptd3j(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_store_for_package_347124r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_subdir_of_gjqk3qb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_summarize_archive_cicsplu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_tree_checksum_k6o2imm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_walk_59naf6k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_clp0sh2cgr2[16] __attribute__((aligned(8)));
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s2kuocg9pm4qc[63];
extern const uint8_t gm_s3m12mc3q4gpj[71];
extern const uint8_t gm_s4ccl84nrk27k[55];
extern const uint8_t gm_s5j185b1e4leb[89];
extern const uint8_t gm_s5ujsjbtr77bt[14];
extern const uint8_t gm_s79r5ktousup8[157];
extern const uint8_t gm_s8ph5bt10ogvd[10];
extern const uint8_t gm_s9da4sg04ql4e[60];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sb6brpqpm5qjh[21];
extern const uint8_t gm_sbdno1ojbbrbs[45];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbjshone12eeu[158];
extern const uint8_t gm_sbpcdsg6ifd91[5];
extern const uint8_t gm_scq0i5n3f7it5[41];
extern const uint8_t gm_sctlci97unb16[36];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_sg39dr0kaebm0[10];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgib96mf5meqv[11];
extern const uint8_t gm_sh0fsro8kchj4[52];
extern const uint8_t gm_sh3r0ji6563do[127];
extern const uint8_t gm_shmsiqao0jnvn[42];
extern const uint8_t gm_shs0aou723lfc[25];
extern const uint8_t gm_sif59l16kubo3[17];
extern const uint8_t gm_sl7562rl6kuvh[7];
extern const uint8_t gm_sm2qh69a2gq0m[51];
extern const uint8_t gm_sn0ocj5vv0ci3[114];
extern const uint8_t gm_sn2t7jm5mf8pm[5];
extern const uint8_t gm_snak43jlv01k6[89];
extern const uint8_t gm_snk3kkdsb32a9[129];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_so597te7oecin[72];
extern const uint8_t gm_soajcqnnsnidh[57];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soqpbc95nno1l[7];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_spc0i9srvnmq5[6];
extern const uint8_t gm_spsjkc9sqdvl6[61];
extern const uint8_t gm_sqlpqagi4dpk1[38];
extern const uint8_t gm_sr0hr1ep2j2vv[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcpsceljs9unl1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_sssjusbjeb8fr[7];
extern const uint8_t gm_ssv5povjldd7c[3];
extern const uint8_t gm_su2m3cb0nts7p[3];
extern const uint8_t gm_svulttobk4q7t[4];
extern const gm_tmpl gm_tpl00b67aeoiuop;
extern const gm_tmpl gm_tpl0emlhhlpmv0s;
extern const gm_tmpl gm_tpl14n6ndgi62j1;
extern const gm_tmpl gm_tpl1lmdkbkeigtd;
extern const gm_tmpl gm_tpl54tnh29jjfju;
extern const gm_tmpl gm_tpl5a7qf3e2brk4;
extern const gm_tmpl gm_tpl6tu8f1h1mg85;
extern const gm_tmpl gm_tpl80avf3gokjno;
extern const gm_tmpl gm_tpl80jqm34ojjrn;
extern const gm_tmpl gm_tplbk03g4ml40ad;
extern const gm_tmpl gm_tplbmf4neusqbo5;
extern const gm_tmpl gm_tplbppmjhpd8ol0;
extern const gm_tmpl gm_tpldvhfe50cnb69;
extern const gm_tmpl gm_tple7msnncsq0hr;
extern const gm_tmpl gm_tpleiobdo4rm4hs;
extern const gm_tmpl gm_tplhgdgs8lv4h7d;
extern const gm_tmpl gm_tplibfhnj0illo7;
extern const gm_tmpl gm_tplidb7upjrao3q;
extern const gm_tmpl gm_tpljc7kqi31f4d2;
extern const gm_tmpl gm_tplm1fuloaqnfv4;
extern const gm_tmpl gm_tplo481o6n7og1g;
extern const gm_tmpl gm_tplo4cs7gli8ts2;
extern const gm_tmpl gm_tplpqhejpqnulcm;
extern const gm_tmpl gm_tplqn8qbim70q76;
extern const gm_tmpl gm_tplqstpsk3jkk1c;
extern const gm_tmpl gm_tplsfkokuc5r1td;
extern const gm_tmpl gm_tplso4fjesdi372;
extern const gm_tmpl gm_tplu2kaori4llai;
extern const gm_tmpl gm_tplugm36655v1ns;
extern const gm_tmpl gm_tpluhfd3hvppdji;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
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
inline __attribute__((aligned(4))) void gm_f_clear_m4blb4l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_m4blb4l F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 80)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_pirklev(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_pirklev F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_qd87n65(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_qd87n65 F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 56)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_rm1b3rc F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_s0loit2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_s0loit2 F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_db_tssgvk0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* url */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_su2m3cb0nts7p));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7 = gm_f_repo_dir_name_6927pg7((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t8 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t5, t7);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_discard_bdpju80 F;
    gm_t6j6smj7qoctu gm_l0; /* d */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_ttln0d0hd70en t3 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_downloads_vdenqu5 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa2h9vonan0ma));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_14fhq9i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_attt3ii(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_attt3ii F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cpdaeui(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cpdaeui F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gsm9gv5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gsm9gv5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hr6df4i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvf8gur(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kvf8gur F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lcqki20(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lcqki20 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mnej5m4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mnej5m4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n8lsnom F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 168)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oihj9aq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oihj9aq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p1sl7gg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p1sl7gg F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pkfv52g F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s0g91u7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s0g91u7 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0c1hioc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0c1hioc F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spsjkc9sqdvl6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(60));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5a7qf3e2brk4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2orhd15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2orhd15 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snk3kkdsb32a9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(128));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldvhfe50cnb69), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3pk07bs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pk07bs F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scq0i5n3f7it5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl80avf3gokjno), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7sll6e8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7sll6e8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sssjusbjeb8fr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6tu8f1h1mg85), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_95e0vi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_95e0vi0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm2qh69a2gq0m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(50));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu2kaori4llai), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9di2rrs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9di2rrs F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8ph5bt10ogvd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsfkokuc5r1td), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ajecpf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ajecpf6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh3r0ji6563do));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(126));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqn8qbim70q76), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_asjuqmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_asjuqmr F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg39dr0kaebm0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplso4fjesdi372), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bpl33sh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bpl33sh F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4ccl84nrk27k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqstpsk3jkk1c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cvqjb45 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn0ocj5vv0ci3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(113));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0emlhhlpmv0s), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dvcii4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dvcii4b F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s79r5ktousup8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(156));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbk03g4ml40ad), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eagbsnc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eagbsnc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh0fsro8kchj4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplidb7upjrao3q), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fj9bgml(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fj9bgml F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sif59l16kubo3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(16));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleiobdo4rm4hs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fnh5q7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fnh5q7b F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb6brpqpm5qjh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplugm36655v1ns), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g1540jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_g1540jg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snak43jlv01k6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(88));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl14n6ndgi62j1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ia6bs6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ia6bs6h F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3m12mc3q4gpj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(70));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhgdgs8lv4h7d), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jth3mgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jth3mgf F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbjshone12eeu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(157));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1lmdkbkeigtd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l821hfj F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqlpqagi4dpk1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl54tnh29jjfju), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lbar0n6 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shmsiqao0jnvn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple7msnncsq0hr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_m5mroph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_m5mroph F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl7562rl6kuvh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpqhejpqnulcm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mv68p71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mv68p71 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shs0aou723lfc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl00b67aeoiuop), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6eprd4 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbg03hl2obk72));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbmf4neusqbo5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n8h20c1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n8h20c1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sctlci97unb16));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplibfhnj0illo7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nsoplg3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nsoplg3 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5j185b1e4leb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(88));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl80jqm34ojjrn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_o83d7mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_o83d7mn F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so597te7oecin));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(71));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplo4cs7gli8ts2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ro2n9te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ro2n9te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2kuocg9pm4qc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluhfd3hvppdji), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rpqj2mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rpqj2mb F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9da4sg04ql4e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplo481o6n7og1g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s20j3pl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soqpbc95nno1l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljc7kqi31f4d2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_u75ve7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u75ve7h F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbdno1ojbbrbs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbppmjhpd8ol0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vakr6l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vakr6l2 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_soajcqnnsnidh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplm1fuloaqnfv4), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_p4tis8s F;
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = (uint8_t)((t2 == ((uint8_t)0ULL)));
    gm_toq2f2q4opoa3 t4 = GM_LD(gm_toq2f2q4opoa3, ((uint8_t *)a0));
    gm_toq2f2q4opoa3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_0k0up67 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_equal_n71njnf F;
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    uint8_t t4 = (gm_eq0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_th2lqbo77pvsc t5 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 16));
        gm_th2lqbo77pvsc t6 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 16));
        uint8_t t7 = (gm_eq0ogca82g1f(((uint8_t *)&t5), ((uint8_t *)&t6)));
        gm_l2 = t7;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_less_9obq0rq F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
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
        uint8_t t11 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t8), ((uint8_t *)&t10));
        F.ret = t11;
        goto gm_fret;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        uint8_t t14 = gm_f_option_less_72r3i7m(&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t12) + 16)), &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t13) + 16)));
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_key_map_136uk7q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_136uk7q F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_254rpp1((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_e8nmg3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_e8nmg3h F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_o444f8n((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_ihrh7ij(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_ihrh7ij F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_2hljflu((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_kjnah27(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_kjnah27 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_6vurufm((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0pjrgaq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pkfv52g();
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
inline __attribute__((aligned(4))) void gm_f_list_1q70459(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1q70459 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hr6df4i();
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
inline __attribute__((aligned(4))) void gm_f_list_254rpp1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_254rpp1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_gsm9gv5();
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
inline __attribute__((aligned(4))) void gm_f_list_2hljflu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2hljflu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s0g91u7();
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
inline __attribute__((aligned(4))) void gm_f_list_6vurufm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6vurufm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oihj9aq();
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
inline __attribute__((aligned(4))) void gm_f_list_agmtpnt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_agmtpnt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_mnej5m4();
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
inline __attribute__((aligned(4))) void gm_f_list_dc4p80b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dc4p80b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_14fhq9i();
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
inline __attribute__((aligned(4))) void gm_f_list_jsujn2k(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jsujn2k F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lcqki20();
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
inline __attribute__((aligned(4))) void gm_f_list_nccb2fo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nccb2fo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_attt3ii();
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
inline __attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_o444f8n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_cpdaeui();
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
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pfs7ocg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n8lsnom();
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
inline __attribute__((aligned(4))) void gm_f_list_qeqmcmu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qeqmcmu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kvf8gur();
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
inline __attribute__((aligned(4))) void gm_f_list_vsi9vun(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vsi9vun F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p1sl7gg();
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
inline __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_locked_entry_4693mpc F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = (uint8_t)(((((uint8_t *)t2) + 64))[40] != 0);
    if (t3) {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t2) + 64), 40);
    } else {
        memset(&(*(gm_tie69pa8pplgc *)gm_out), 0, sizeof (*(gm_tie69pa8pplgc *)gm_out));
        goto gm_fdone;
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_th2lqbo77pvsc t6 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 16));
        gm_f_find_locked_83oh60j((void *)&(*(gm_tie69pa8pplgc *)gm_out), t4, t5, &t6);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_read_only_a29eok8 F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svulttobk4q7t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
    gm_b16_8 t3 = gm_l0;
    gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_writable_6uascrq F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spc0i9srvnmq5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_b16_8 t3 = gm_l0;
    gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_rnfkbb9 F;
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
        memset(&(*(gm_ta2tdjm589013 *)gm_out), 0, sizeof (*(gm_ta2tdjm589013 *)gm_out));
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
        gm_tk9n3vlila9qg t25 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ta2tdjm589013 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ta2tdjm589013 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ta2tdjm589013 *)gm_out) + 44, 0);
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)&(*(gm_ta2tdjm589013 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_ta2tdjm589013 *)gm_out))[40] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1htqqaf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1htqqaf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_n8lsnom();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_cr2718s(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 168), ((uint8_t *)&F.l1), 168);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 168)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7mbittu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7mbittu F;
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
        gm_b16_8 t9 = gm_f_empty_slice_14fhq9i();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_qn6ph56(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d61dqrj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d61dqrj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_kvf8gur();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o2oupjk(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fp6nnvl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_fp6nnvl F;
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
        gm_b16_8 t9 = gm_f_empty_slice_attt3ii();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ptf2p0d(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_i535kr1 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_pkfv52g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_isbo8va(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ljifgtt(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ljifgtt F;
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
        gm_b16_8 t9 = gm_f_empty_slice_hr6df4i();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_6apusur(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mt615h0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mt615h0 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_mnej5m4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vdf56ao(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)a1), 64);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 64)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r6o112l(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r6o112l F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lcqki20();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ibbj0r1(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)a1), 120);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgb8k63(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vgb8k63 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_p1sl7gg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_moan1lp(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 96), ((uint8_t *)a1), 96);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 96)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6apusur(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_6apusur F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cr2718s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cr2718s F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)168ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ibbj0r1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ibbj0r1 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_isbo8va(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_isbo8va F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_moan1lp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_moan1lp F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)96ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o2oupjk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o2oupjk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ptf2p0d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ptf2p0d F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qn6ph56(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_qn6ph56 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vdf56ao(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vdf56ao F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_tree_rp98g1s F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b0_1 t3 = gm_f_make_writable_6uascrq((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_l0;
    gm_ttln0d0hd70en t5 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_req_same_h2pl2bq F;
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    uint8_t t4 = (gm_eq0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 16));
        uint8_t t7 = (gm_eq0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6)));
        gm_l4 = t7;
    } else {
        gm_l4 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l4;
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 32));
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        gm_l3 = t11;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        uint8_t t13 = gm_f_spec_equal_foa1umn(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a0) + 48)), &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a1) + 48)));
        gm_l2 = t13;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_short_rev_uin3i2s F;
    gm_b16_8 gm_l0; /* rev */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l1;
    void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t5 = (t4 < ((uint64_t)12ULL));
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        gm_l1 = t6;
    } else {
        gm_l1 = ((uint64_t)12ULL);
    }
    uint64_t t7 = gm_l1;
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t8, t9, t3, gm_srcpsceljs9unl1, 516, 43);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_1kptd3j(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_1kptd3j F;
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
    gm_f_introsort_h9phkod(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
    gm_f_introsort_r6p6ckl(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_f_strip_prefix_vl3t0h4((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_req_klu87ju(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_req_klu87ju F;
    void * gm_l0; /* r */
    void * gm_l4; /* reqs */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 112);
        void * t9 = gm_f_slot_9ns6k3q(t8, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_l8 = t9;
    }
    uint8_t t10 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
    if (t10) {
        void * t11 = GM_LD(void *, ((uint8_t *)&gm_l8));
        gm_l4 = t11;
    } else {
        {
            gm_trqfvmjtt4sij t12;
            gm_f_list_vsi9vun((void *)&t12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t12;
        }
        {
            void * t13 = (void *)((uint8_t *)&F.l3);
            gm_tftb1odc739t5 t14 = gm_f_push_vgb8k63(t13, &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
            gm_l9 = t14;
        }
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcpsceljs9unl1, 407, 9);
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 112);
            gm_b0_1 t18 = gm_f_put_ui22st8((void *)NULL, cx, NULL, t17, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t19 = gm_l0;
            gm_b0_1 t20 = gm_f_after_req_ti0a6un((void *)NULL, cx, NULL, t19, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t21 = gm_l4;
    gm_l4 = t21;
    void * t22 = gm_l4;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
    gm_l5 = t23;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l6;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t29 = gm_l6;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l7), (((uint8_t *)t27) + (uintptr_t)t29 * 96), 96);
                        }
                        {
                            uint8_t t30 = gm_f_req_same_h2pl2bq(&(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l7)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
                            if (t30) {
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 412, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l6;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l6 = t34;
        }
    }
gm_lb1:;
    {
        void * t35 = gm_l4;
        gm_tftb1odc739t5 t36 = gm_f_push_vgb8k63(t35, &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
        gm_l10 = t36;
    }
    uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcpsceljs9unl1, 415, 5);
    {
        void * t38 = gm_l0;
        gm_b0_1 t39 = gm_f_after_req_ti0a6un((void *)NULL, cx, NULL, t38, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_after_req_ti0a6un(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_after_req_ti0a6un F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tgi1k2malmpgi gm_l4;
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 312);
        gm_t6ok5tvcjc200 t6 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
        gm_f_get_a0eoncd((void *)&gm_l4, t5, &t6);
    }
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l4))[56] != 0);
    if (t7) {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l4), 56);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            {
                uint8_t t8 = gm_f_is_default_p4tis8s(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a2) + 48)));
                uint8_t t9 = (uint8_t)!t8;
                if (!t9) {
                    goto gm_lb2;
                }
            }
            {
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                uint8_t t12 = gm_f_satisfies_mlodg56((void *)NULL, cx, NULL, t10, t11, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a2) + 48)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t13 = (uint8_t)!t12;
                if (!t13) {
                    goto gm_lb2;
                }
            }
            {
                void * t14 = gm_l0;
                void * t15 = (void *)(((uint8_t *)t14) + 312);
                gm_f_drop_nns2kfo(t15, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            }
            void * t16 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t16) + 552), ((uint8_t)1));
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
inline __attribute__((aligned(4))) void gm_f_archive_info_gq6dkee(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_archive_info_gq6dkee F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 232));
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 64), 64);
                        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&F.l4));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 65, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 66, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 68, 0);
                            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out)), t18);
                            ((uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out))[64] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 610, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
        }
    }
gm_lb1:;
    memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ti6pdj2sunpe4 gm_f_archive_info_gq6dkee_c(void * a0, gm_b16_8 a1) { gm_ti6pdj2sunpe4 gm_r; gm_f_archive_info_gq6dkee(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_build_lock_hmfdkj8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_build_lock_hmfdkj8 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* checksum */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l20; /* url */
    GM_STACK_CHECK(&F, 2635u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l22;
    gm_tgi1k2malmpgi gm_l23;
    gm_th2lqbo77pvsc gm_l24;
    gm_tie69pa8pplgc gm_l25;
    gm_b16_8 gm_l26;
    gm_th2lqbo77pvsc gm_l27;
    gm_t3ujhu6qph7de gm_l28;
    gm_th2lqbo77pvsc gm_l29;
    gm_t3ujhu6qph7de gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_th2lqbo77pvsc gm_l33;
    gm_ti6pdj2sunpe4 gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tftb1odc739t5 gm_l36;
    {
        gm_trqfvmjtt4sij t26;
        gm_f_list_pfs7ocg((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t26;
        gm_trqfvmjtt4sij t27;
        gm_f_list_qeqmcmu((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t27;
        void * t28 = gm_l0;
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 312));
        gm_l3 = t29;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t30 = gm_l4;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t32 = (t30 < t31);
                if (!t32) {
                    goto gm_lb1;
                }
                {
                    void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t35 = gm_l4;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l5), (((uint8_t *)t33) + (uintptr_t)t35 * 96), 96);
                            }
                            {
                                void * t36 = (void *)((uint8_t *)&F.l2);
                                gm_tftb1odc739t5 t37 = gm_f_push_d61dqrj(t36, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l5)));
                                gm_l22 = t37;
                            }
                            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcpsceljs9unl1, 836, 39);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 836, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t39 = gm_l4;
                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                uint64_t t42 = r40;
                gm_l4 = t42;
            }
        }
gm_lb1:;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_closure t44 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_key_less_9obq0rq)), NULL, cx});
        gm_f_sort_by_1kptd3j(t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_l6 = t45;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t46 = gm_l7;
                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t48 = (t46 < t47);
                if (!t48) {
                    goto gm_lb6;
                }
                {
                    void * t49 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t51 = gm_l7;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)t49) + (uintptr_t)t51 * 40), 40);
                            }
                            {
                                void * t52 = gm_l0;
                                void * t53 = (void *)(((uint8_t *)t52) + 312);
                                gm_t6ok5tvcjc200 t54 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l8));
                                gm_f_get_a0eoncd((void *)&gm_l23, t53, &t54);
                            }
                            uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l23))[56] != 0);
                            if (!t55) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 839, 19);
                            {
                                memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l23), 56);
                            }
                            gm_b16_8 t56;
                            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(0));
                            gm_l10 = t56;
                            {
                                {
                                    {
                                        void * t57 = gm_l0;
                                        void * t58 = (void *)(((uint8_t *)t57) + 432);
                                        gm_t6ok5tvcjc200 t59 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l8));
                                        gm_f_get_vc34oj2((void *)&gm_l24, t58, &t59);
                                    }
                                    uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l24))[16] != 0);
                                    if (!t60) {
                                        goto gm_lb12;
                                    }
                                    gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                                    gm_l11 = t61;
                                    gm_b16_8 t62 = gm_l11;
                                    gm_l10 = t62;
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    {
                                        {
                                            void * t63 = gm_l0;
                                            gm_t6ok5tvcjc200 t64 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l8));
                                            gm_f_locked_entry_4693mpc((void *)&gm_l25, t63, &t64);
                                        }
                                        uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l25))[168] != 0);
                                        if (!t65) {
                                            goto gm_lb14;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l25), 168);
                                        }
                                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 96));
                                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                        uint8_t t68 = (gm_eq0iqiubfm7e(((uint8_t *)&t66), ((uint8_t *)&t67)));
                                        if (t68) {
                                            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 136));
                                            gm_l26 = t69;
                                        } else {
                                            {
                                                void * t70 = gm_l0;
                                                void * t71 = (void *)(((uint8_t *)t70) + 392);
                                                gm_t6ok5tvcjc200 t72 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l8));
                                                gm_f_get_vc34oj2((void *)&gm_l27, t71, &t72);
                                            }
                                            uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
                                            if (!t73) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 844, 87);
                                            {
                                                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                gm_f_tree_checksum_k6o2imm((void *)&gm_l28, (void *)NULL, cx, NULL, t74);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                            if (t75) {
                                            } else {
                                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                                                ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t76);
                                                goto gm_fdone;
                                            }
                                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                            gm_l26 = t77;
                                        }
                                        gm_b16_8 t78 = gm_l26;
                                        gm_l10 = t78;
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        void * t79 = gm_l0;
                                        void * t80 = (void *)(((uint8_t *)t79) + 392);
                                        gm_t6ok5tvcjc200 t81 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l8));
                                        gm_f_get_vc34oj2((void *)&gm_l29, t80, &t81);
                                    }
                                    uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l29))[16] != 0);
                                    if (!t82) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 846, 42);
                                    {
                                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l29));
                                        gm_f_tree_checksum_k6o2imm((void *)&gm_l30, (void *)NULL, cx, NULL, t83);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                    if (t84) {
                                    } else {
                                        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                                        ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t85);
                                        goto gm_fdone;
                                    }
                                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                                    gm_l10 = t86;
                                }
gm_lb13:;
                            }
gm_lb11:;
                            {
                                void * t87 = (void *)((uint8_t *)&F.l1);
                                void * t88 = gm_l0;
                                gm_b16_8 t89 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t88, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 40));
                                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                gm_th2lqbo77pvsc t93;
                                memset(&t93, 0, sizeof t93);
                                gm_b16_8 t94 = gm_l10;
                                void * t95 = gm_l0;
                                gm_th2lqbo77pvsc t96 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l8) + 16));
                                gm_th2lqbo77pvsc t97 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l9) + 16));
                                gm_b16_8 t98 = gm_f_dependency_names_6m0jc80((void *)NULL, cx, NULL, t95, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ts1nru0t7g7aj t99;
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t89);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t90);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t99 + 32), t96);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t99 + 56), t97);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 80), t91);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 96), t92);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t99 + 112), t93);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 136), t94);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 152), t98);
                                gm_tftb1odc739t5 t100 = gm_f_push_1htqqaf(t87, &t99);
                                gm_l31 = t100;
                            }
                            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcpsceljs9unl1, 848, 9);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 838, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t102 = gm_l7;
                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                uint64_t t105 = r103;
                gm_l7 = t105;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t106;
        gm_f_list_qeqmcmu((void *)&t106, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t106;
        void * t107 = gm_l0;
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)t107) + 352));
        gm_l14 = t108;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t109 = gm_l15;
                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t111 = (t109 < t110);
                if (!t111) {
                    goto gm_lb15;
                }
                {
                    void * t112 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t114 = gm_l15;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l16), (((uint8_t *)t112) + (uintptr_t)t114 * 40), 40);
                            }
                            {
                                void * t115 = (void *)((uint8_t *)&F.l13);
                                gm_tftb1odc739t5 t116 = gm_f_push_d61dqrj(t115, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l16)));
                                gm_l32 = t116;
                            }
                            uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcpsceljs9unl1, 861, 38);
                            goto gm_lb18;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 861, 5);
                    }
gm_lb18:;
                }
gm_lb17:;
                uint64_t t118 = gm_l15;
                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                uint64_t t121 = r119;
                gm_l15 = t121;
            }
        }
gm_lb15:;
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_closure t123 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_key_less_9obq0rq)), NULL, cx});
        gm_f_sort_by_1kptd3j(t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_l17 = t124;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t125 = gm_l18;
                uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t127 = (t125 < t126);
                if (!t127) {
                    goto gm_lb20;
                }
                {
                    void * t128 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t130 = gm_l18;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l19), (((uint8_t *)t128) + (uintptr_t)t130 * 40), 40);
                            }
                            {
                                gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                gm_b16_8 t132;
                                GM_ST(void *, (uint8_t *)&t132, (void *)(gm_s2j8t1jqamft8));
                                GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(4));
                                gm_f_strip_prefix_b4949s8((void *)&gm_l33, t131, t132);
                            }
                            uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l33))[16] != 0);
                            if (t133) {
                                gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l33));
                                gm_l20 = t134;
                            } else {
                                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                gm_l20 = t135;
                            }
                            {
                                void * t136 = gm_l0;
                                gm_b16_8 t137 = gm_l20;
                                gm_f_archive_info_gq6dkee((void *)&gm_l34, t136, t137);
                            }
                            uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l34))[64] != 0);
                            if (!t138) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 865, 17);
                            {
                                memmove(((uint8_t *)&F.l21), ((uint8_t *)&gm_l34), 64);
                            }
                            {
                                void * t139 = (void *)((uint8_t *)&F.l1);
                                void * t140 = gm_l0;
                                gm_b16_8 t141 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t140, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l19)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                gm_th2lqbo77pvsc t143;
                                memset(&t143, 0, sizeof t143);
                                gm_b16_8 t144;
                                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(0));
                                gm_b16_8 t145;
                                GM_ST(void *, (uint8_t *)&t145, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(0));
                                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 16));
                                gm_tnpifv20irnph t147;
                                GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
                                gm_th2lqbo77pvsc t148 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 16));
                                gm_f_format_9di2rrs((void *)&gm_l35, (void *)NULL, cx, NULL, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcpsceljs9unl1, 873, 22);
                                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                                gm_th2lqbo77pvsc t152;
                                GM_ST(uint8_t, (uint8_t *)&t152 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t152 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t152 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t152), t151);
                                ((uint8_t *)&t152)[16] = 1;
                                gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 48));
                                void * t154 = gm_l0;
                                gm_b16_8 t155 = gm_f_dependency_names_6m0jc80((void *)NULL, cx, NULL, t154, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l19)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ts1nru0t7g7aj t156;
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t141);
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 16), t142);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t156 + 32), t148);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t156 + 56), t143);
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 80), t144);
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 96), t145);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t156 + 112), t152);
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 136), t153);
                                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 152), t155);
                                gm_tftb1odc739t5 t157 = gm_f_push_1htqqaf(t139, &t156);
                                gm_l36 = t157;
                            }
                            uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                            if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcpsceljs9unl1, 866, 9);
                            goto gm_lb23;
                        }
gm_lb24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 863, 5);
                    }
gm_lb23:;
                }
gm_lb22:;
                uint64_t t159 = gm_l18;
                uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                uint64_t t162 = r160;
                gm_l18 = t162;
            }
        }
gm_lb20:;
    }
    {
        gm_trqfvmjtt4sij t163 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        gm_tin97tt4ud0s1 t164;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t164 + 0), t163);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 0;
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t164);
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
__attribute__((aligned(4))) void gm_f_check_refresh_names_9ih3ok9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_refresh_names_9ih3ok9 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* n */
    gm_b16_8 gm_l2; /* names */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* n */
    uint8_t gm_l6; /* known */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* u */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 854u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l19;
    void * t30 = gm_l0;
    {
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 24) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
            if (!t32) {
                goto gm_lb2;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t30) + 24) + 8));
            gm_l1 = t33;
            gm_b16_8 t34 = gm_l1;
            gm_l2 = t34;
            goto gm_lb1;
        }
gm_lb2:;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
            goto gm_fdone;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 372, 14);
    }
gm_lb1:;
    gm_b16_8 t35 = gm_l2;
    gm_l3 = t35;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t36 = gm_l4;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t38 = (t36 < t37);
            if (!t38) {
                goto gm_lb4;
            }
            {
                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t41 = gm_l4;
                {
                    {
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t39) + (uintptr_t)t41 * 16));
                        gm_l5 = t42;
                        gm_l6 = ((uint8_t)0);
                        void * t43 = gm_l0;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t43) + 312));
                        gm_l7 = t44;
                        gm_l8 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t45 = gm_l8;
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t47 = (t45 < t46);
                                if (!t47) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t50 = gm_l8;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l9), (((uint8_t *)t48) + (uintptr_t)t50 * 96), 96);
                                            }
                                            {
                                                void * t51 = gm_l0;
                                                gm_b16_8 t52 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t51, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l9)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_b16_8 t53 = gm_l5;
                                                uint8_t t54 = (gm_eq0iqiubfm7e(((uint8_t *)&t52), ((uint8_t *)&t53)));
                                                if (t54) {
                                                    gm_l6 = ((uint8_t)1);
                                                }
                                            }
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 378, 9);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t55 = gm_l8;
                                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                uint64_t t58 = r56;
                                gm_l8 = t58;
                            }
                        }
gm_lb9:;
                        void * t59 = gm_l0;
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)t59) + 272));
                        gm_l10 = t60;
                        gm_l11 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t61 = gm_l11;
                                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint8_t t63 = (t61 < t62);
                                if (!t63) {
                                    goto gm_lb14;
                                }
                                {
                                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint64_t t66 = gm_l11;
                                    {
                                        {
                                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)t64) + (uintptr_t)t66 * 16));
                                            gm_l12 = t67;
                                            gm_b16_8 t68 = gm_l12;
                                            gm_b16_8 t69 = gm_l5;
                                            uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                                            if (t70) {
                                                gm_l6 = ((uint8_t)1);
                                            }
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 381, 9);
                                    }
gm_lb17:;
                                }
gm_lb16:;
                                uint64_t t71 = gm_l11;
                                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                                uint64_t t74 = r72;
                                gm_l11 = t74;
                            }
                        }
gm_lb14:;
                        void * t75 = gm_l0;
                        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 112));
                        gm_l13 = t76;
                        gm_l14 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t77 = gm_l14;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint8_t t79 = (t77 < t78);
                                if (!t79) {
                                    goto gm_lb19;
                                }
                                {
                                    void * t80 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint64_t t82 = gm_l14;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l15), (((uint8_t *)t80) + (uintptr_t)t82 * 80), 80);
                                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 40));
                                            gm_l16 = t83;
                                            gm_l17 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t84 = gm_l17;
                                                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                    uint8_t t86 = (t84 < t85);
                                                    if (!t86) {
                                                        goto gm_lb24;
                                                    }
                                                    {
                                                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                        uint64_t t89 = gm_l17;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t87) + (uintptr_t)t89 * 96), 96);
                                                                gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 16));
                                                                gm_b16_8 t91 = gm_l5;
                                                                uint8_t t92 = (gm_eq0iqiubfm7e(((uint8_t *)&t90), ((uint8_t *)&t91)));
                                                                if (t92) {
                                                                    gm_l6 = ((uint8_t)1);
                                                                }
                                                                goto gm_lb27;
                                                            }
gm_lb28:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 385, 13);
                                                        }
gm_lb27:;
                                                    }
gm_lb26:;
                                                    uint64_t t93 = gm_l17;
                                                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                                    uint64_t t96 = r94;
                                                    gm_l17 = t96;
                                                }
                                            }
gm_lb24:;
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 384, 9);
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                uint64_t t97 = gm_l14;
                                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                uint64_t t100 = r98;
                                gm_l14 = t100;
                            }
                        }
gm_lb19:;
                        uint8_t t101 = gm_l6;
                        uint8_t t102 = (uint8_t)!t101;
                        if (t102) {
                            {
                                gm_b16_8 t103 = gm_l5;
                                gm_tnpifv20irnph t104;
                                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                                gm_f_format_u75ve7h((void *)&gm_l19, (void *)NULL, cx, NULL, t104);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcpsceljs9unl1, 389, 32);
                            {
                                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t107);
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 376, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t108 = gm_l4;
            uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
            uint64_t t111 = r109;
            gm_l4 = t111;
        }
    }
gm_lb4:;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
__attribute__((aligned(4))) void gm_f_choose_9ee155u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_choose_9ee155u F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* reqs */
    uint8_t gm_l5; /* reuse */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l12; /* dep */
    gm_b16_8 gm_l13; /* url */
    GM_STACK_CHECK(&F, 1622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tgi1k2malmpgi gm_l15;
    gm_tqetrgo0q24b5 gm_l16;
    gm_trqfvmjtt4sij gm_l17;
    uint8_t gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tl572d7579hns gm_l21;
    gm_tcmp1j2cnqi8i gm_l22;
    gm_tmfqp43enl3pa gm_l23;
    {
        {
            {
                void * t16 = gm_l0;
                void * t17 = (void *)(((uint8_t *)t16) + 312);
                gm_t6ok5tvcjc200 t18 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_a0eoncd((void *)&gm_l15, t17, &t18);
            }
            uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l15))[56] != 0);
            if (!t19) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l15), 56);
            }
            {
                gm_t8qqrlhb610a2 t20 = GM_LD(gm_t8qqrlhb610a2, ((uint8_t *)&F.l2));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
                GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t20);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 112);
        gm_t6ok5tvcjc200 t23 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
        gm_f_get_9fn35sd((void *)&gm_l16, t22, &t23);
    }
    uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l16))[40] != 0);
    if (!t24) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 473, 13);
    {
        memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 40);
    }
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
    gm_l3 = t25;
    {
        void * t26 = gm_l0;
        gm_t6ok5tvcjc200 t27 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
        gm_f_locked_entry_4693mpc((void *)&F.l4, t26, &t27);
    }
    gm_l5 = ((uint8_t)0);
    {
        {
            uint8_t t28 = (uint8_t)((((uint8_t *)&F.l4))[168] != 0);
            if (!t28) {
                goto gm_lb4;
            }
            {
                memmove(((uint8_t *)&F.l6), ((uint8_t *)&F.l4), 168);
            }
            {
                void * t29 = gm_l0;
                uint8_t t30 = gm_f_is_refreshed_ifi7dst((void *)NULL, cx, NULL, t29, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t31 = (uint8_t)!t30;
                if (t31) {
                    gm_l5 = ((uint8_t)1);
                    gm_b16_8 t32 = gm_l3;
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                    gm_b16_8 t34 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t34;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t35 = gm_l8;
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t37 = (t35 < t36);
                            if (!t37) {
                                goto gm_lb5;
                            }
                            {
                                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t40 = gm_l8;
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l9), (((uint8_t *)t38) + (uintptr_t)t40 * 96), 96);
                                        }
                                        {
                                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 96));
                                            uint8_t t43 = gm_f_satisfies_mlodg56((void *)NULL, cx, NULL, t41, t42, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l9) + 48)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t44 = (uint8_t)!t43;
                                            if (t44) {
                                                gm_l5 = ((uint8_t)0);
                                            }
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 479, 13);
                                }
gm_lb8:;
                            }
gm_lb7:;
                            uint64_t t45 = gm_l8;
                            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                            uint64_t t48 = r46;
                            gm_l8 = t48;
                        }
                    }
gm_lb5:;
                }
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(0));
        gm_th2lqbo77pvsc t50;
        memset(&t50, 0, sizeof t50);
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 0), t49);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l10 + 16), t50);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 40), t51);
    }
    uint8_t t52 = gm_l5;
    if (t52) {
        uint8_t t53 = (uint8_t)((((uint8_t *)&F.l4))[168] != 0);
        if (!t53) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 486, 14);
        {
            memmove(((uint8_t *)&F.l11), ((uint8_t *)&F.l4), 168);
        }
        {
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 96));
            gm_b16_8 t55 = gm_l3;
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 80));
            gm_th2lqbo77pvsc t58;
            gm_f_recorded_version_lb5b4vt((void *)&t58, (void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 80));
            GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 0), t54);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l10 + 16), t58);
            GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 40), t59);
        }
    } else {
        void * t60 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_srcpsceljs9unl1, 491, 16);
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 96) + 16));
        gm_l12 = t62;
        void * t63 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t64, gm_srcpsceljs9unl1, 492, 16);
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((((uint8_t *)t63) + (uintptr_t)((uint64_t)0ULL) * 96) + 32));
        gm_l13 = t65;
        {
            {
                void * t66 = gm_l0;
                uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t66) + 16) + 1));
                if (!t67) {
                    goto gm_lb11;
                }
                void * t68 = gm_l0;
                {
                    {
                        uint8_t t69 = (uint8_t)(((((uint8_t *)t68) + 64))[40] != 0);
                        if (t69) {
                            goto gm_lb13;
                        }
                        gm_l18 = ((uint8_t)1);
                        goto gm_lb12;
                    }
gm_lb13:;
                    {
                        gm_l18 = ((uint8_t)0);
                        goto gm_lb12;
                    }
gm_lb14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 493, 30);
                }
gm_lb12:;
                uint8_t t70 = gm_l18;
                if (!t70) {
                    goto gm_lb11;
                }
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)gm_clp0sh2cgr2));
                memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t71);
                goto gm_fdone;
            }
gm_lb11:;
        }
gm_lb10:;
        void * t72 = gm_l0;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 16) + 1));
        if (t73) {
            {
                gm_b16_8 t74 = gm_l12;
                gm_b16_8 t75 = gm_l13;
                gm_t6j6smj7qoctu t76;
                GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t74);
                GM_ST(gm_b16_8, ((uint8_t *)&t76 + 16), t75);
                gm_f_format_cvqjb45((void *)&gm_l19, (void *)NULL, cx, NULL, &t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcpsceljs9unl1, 495, 21);
            {
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t79);
                goto gm_fdone;
            }
        }
        void * t80 = gm_l0;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 16));
        if (t81) {
            {
                gm_b16_8 t82 = gm_l12;
                gm_b16_8 t83 = gm_l13;
                gm_t6j6smj7qoctu t84;
                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t82);
                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 16), t83);
                gm_f_format_ajecpf6((void *)&gm_l20, (void *)NULL, cx, NULL, &t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcpsceljs9unl1, 502, 21);
            {
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t87);
                goto gm_fdone;
            }
        }
        {
            void * t88 = gm_l0;
            gm_t6ok5tvcjc200 t89 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
            gm_f_mirror_8u3u6oe((void *)&gm_l21, (void *)NULL, cx, NULL, t88, &t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (t90) {
        } else {
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t91);
            goto gm_fdone;
        }
        {
            void * t92 = gm_l0;
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_f_refs_of_jfglrbh((void *)&gm_l22, t92, t93);
        }
        uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l22))[104] != 0);
        if (!t94) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 509, 17);
        {
            memmove(((uint8_t *)&F.l14), ((uint8_t *)&gm_l22), 104);
        }
        {
            gm_b16_8 t95 = gm_l13;
            gm_b16_8 t96 = gm_l3;
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            void * t98 = (void *)((uint8_t *)&F.l14);
            void * t99 = gm_l0;
            void * t100 = (void *)(((uint8_t *)t99) + 48);
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_b16_8 t102 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_pick_ronijl9((void *)&gm_l23, (void *)NULL, cx, NULL, t95, t97, t98, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (t103) {
        } else {
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t104);
            goto gm_fdone;
        }
        {
            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l23) + 8), 56);
        }
    }
    {
        void * t105 = gm_l0;
        void * t106 = (void *)(((uint8_t *)t105) + 312);
        gm_b0_1 t107 = gm_f_put_4eaoh8f((void *)NULL, cx, NULL, t106, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t8qqrlhb610a2 t108 = GM_LD(gm_t8qqrlhb610a2, ((uint8_t *)&F.l10));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t108);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_dependency_names_6m0jc80(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_dependency_names_6m0jc80 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* n */
    GM_STACK_CHECK(&F, 719u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqetrgo0q24b5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tf1bjg81u0glv gm_l14;
    gm_tftb1odc739t5 gm_l15;
    {
        gm_trqfvmjtt4sij t16;
        gm_f_list_epvducg((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t16;
    }
    {
        {
            {
                void * t17 = gm_l0;
                void * t18 = (void *)(((uint8_t *)t17) + 512);
                gm_t6ok5tvcjc200 t19 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_7q4fb48((void *)&gm_l12, t18, &t19);
            }
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l12))[40] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l12), 40);
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_l4 = t21;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t24 = (t22 < t23);
                    if (!t24) {
                        goto gm_lb3;
                    }
                    {
                        void * t25 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t27 = gm_l5;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l6), (((uint8_t *)t25) + (uintptr_t)t27 * 40), 40);
                                }
                                {
                                    void * t28 = (void *)((uint8_t *)&F.l2);
                                    void * t29 = gm_l0;
                                    gm_b16_8 t30 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t29, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l6)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t28, t30);
                                    gm_l13 = t31;
                                }
                                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcpsceljs9unl1, 816, 31);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 816, 9);
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
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_f_sort_skeree7(t37);
    {
        gm_trqfvmjtt4sij t38;
        gm_f_list_epvducg((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t38;
    }
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
    gm_l8 = t40;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t41 = gm_l9;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t43 = (t41 < t42);
            if (!t43) {
                goto gm_lb8;
            }
            {
                uint64_t t44 = gm_l9;
                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t47 = gm_l9;
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)t47 * 16));
                GM_ST(uint64_t, ((uint8_t *)&gm_l14 + 0), t44);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l14 + 8), t48);
                {
                    {
                        uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l10 = t49;
                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        gm_l11 = t50;
                        {
                            {
                                uint64_t t51 = gm_l10;
                                uint8_t t52 = (t51 > ((uint64_t)0ULL));
                                if (!t52) {
                                    goto gm_lb14;
                                }
                                void * t53 = GM_LD(void *, ((uint8_t *)&F.l2));
                                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                uint64_t t55 = gm_l10;
                                uint64_t r56; bool o57 = __builtin_sub_overflow(t55, ((uint64_t)1ULL), &r56);
                                if (o57) gm_trap("integer overflow in `-`", gm_srcpsceljs9unl1, 821, 34);
                                uint64_t t58 = r56;
                                if ((uintptr_t)t58 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t58, t54, gm_srcpsceljs9unl1, 821, 22);
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)t58 * 16));
                                gm_b16_8 t60 = gm_l11;
                                uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                                if (!t61) {
                                    goto gm_lb14;
                                }
                                goto gm_lb10;
                            }
gm_lb14:;
                        }
gm_lb13:;
                        void * t62 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t63 = gm_l11;
                        gm_tftb1odc739t5 t64 = gm_f_push_3ef746b(t62, t63);
                        gm_l15 = t64;
                        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcpsceljs9unl1, 822, 9);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 820, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t66 = gm_l9;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            uint64_t t69 = r67;
            gm_l9 = t69;
        }
    }
gm_lb8:;
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
    F.ret = t71;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_mp0u2mq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_describe_mp0u2mq F;
    gm_b16_8 gm_l0; /* reqs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* p */
    GM_STACK_CHECK(&F, 792u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tf1bjg81u0glv gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    {
        gm_trqfvmjtt4sij t14;
        gm_f_list_epvducg((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t14;
    }
    gm_b16_8 t15 = gm_l0;
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
                        {
                            memmove(((uint8_t *)&F.l4), (((uint8_t *)t19) + (uintptr_t)t21 * 96), 96);
                        }
                        {
                            void * t22 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                            gm_b16_8 t24 = gm_f_spec_text_9k2gjft((void *)NULL, cx, NULL, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l4) + 48)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t25;
                            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t23);
                            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t24);
                            gm_f_format_fj9bgml((void *)&gm_l10, (void *)NULL, cx, NULL, &t25);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcpsceljs9unl1, 124, 32);
                            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                            gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t22, t28);
                            gm_l11 = t29;
                        }
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcpsceljs9unl1, 124, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 124, 5);
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
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_f_sort_skeree7(t35);
    {
        gm_trqfvmjtt4sij t36;
        gm_f_list_epvducg((void *)&t36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t36;
    }
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
    gm_l6 = t38;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t39 = gm_l7;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t41 = (t39 < t40);
            if (!t41) {
                goto gm_lb6;
            }
            {
                uint64_t t42 = gm_l7;
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t45 = gm_l7;
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t43) + (uintptr_t)t45 * 16));
                GM_ST(uint64_t, ((uint8_t *)&gm_l12 + 0), t42);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l12 + 8), t46);
                {
                    {
                        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                        gm_l8 = t47;
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                        gm_l9 = t48;
                        {
                            {
                                uint64_t t49 = gm_l8;
                                uint8_t t50 = (t49 > ((uint64_t)0ULL));
                                if (!t50) {
                                    goto gm_lb12;
                                }
                                void * t51 = GM_LD(void *, ((uint8_t *)&F.l1));
                                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
                                uint64_t t53 = gm_l8;
                                uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
                                if (o55) gm_trap("integer overflow in `-`", gm_srcpsceljs9unl1, 128, 34);
                                uint64_t t56 = r54;
                                if ((uintptr_t)t56 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t56, t52, gm_srcpsceljs9unl1, 128, 22);
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t51) + (uintptr_t)t56 * 16));
                                gm_b16_8 t58 = gm_l9;
                                uint8_t t59 = (gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                if (!t59) {
                                    goto gm_lb12;
                                }
                                goto gm_lb8;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        void * t60 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t61 = gm_l9;
                        gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t60, t61);
                        gm_l13 = t62;
                        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcpsceljs9unl1, 129, 9);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 127, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t64 = gm_l7;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            uint64_t t67 = r65;
            gm_l7 = t67;
        }
    }
gm_lb6:;
    {
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l14, (void *)NULL, cx, NULL, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcpsceljs9unl1, 131, 12);
    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
    F.ret = t73;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effective_vikkdqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_effective_vikkdqh F;
    gm_b16_8 gm_l0; /* reqs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_vsi9vun((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_b16_8 t8 = gm_l0;
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
                        {
                            memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 96), 96);
                        }
                        {
                            uint8_t t15 = gm_f_is_default_p4tis8s(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l4) + 48)));
                            uint8_t t16 = (uint8_t)!t15;
                            if (t16) {
                                void * t17 = (void *)((uint8_t *)&F.l1);
                                gm_tftb1odc739t5 t18 = gm_f_push_vgb8k63(t17, &(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l4)));
                                gm_l5 = t18;
                                uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                                if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcpsceljs9unl1, 81, 37);
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 80, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l3;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l3 = t23;
        }
    }
gm_lb1:;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
    uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
    if (t25) {
        gm_b16_8 t26 = gm_l0;
        F.ret = t26;
        goto gm_fret;
    }
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    F.ret = t28;
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
__attribute__((aligned(4))) void gm_f_ensure_archive_70kh53q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_ensure_archive_70kh53q F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* dep */
    gm_b16_8 gm_l4; /* h */
    gm_b16_8 gm_l5; /* lock_key */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_th2lqbo77pvsc gm_l11; /* locked_hash */
    gm_b16_8 gm_l13; /* a */
    gm_th2lqbo77pvsc gm_l14; /* expected_hash */
    gm_b16_8 gm_l15; /* origin */
    gm_b16_8 gm_l16; /* h */
    gm_b16_8 gm_l17; /* h */
    gm_th2lqbo77pvsc gm_l18; /* locked_sum */
    gm_b16_8 gm_l20; /* h */
    gm_b16_8 gm_l21; /* h */
    gm_b16_8 gm_l22; /* dest */
    gm_t6j6smj7qoctu gm_l24; /* dl */
    gm_b16_8 gm_l25; /* h */
    gm_b16_8 gm_l26; /* dest */
    gm_b16_8 gm_l27; /* e */
    gm_b16_8 gm_l28; /* e */
    gm_b16_8 gm_l29; /* sum */
    gm_b16_8 gm_l30; /* l */
    gm_b16_8 gm_l31; /* dir */
    GM_STACK_CHECK(&F, 3859u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_ti6pdj2sunpe4 gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_b16_8 gm_l36;
    gm_th2lqbo77pvsc gm_l37;
    gm_tabb710d9mq2e gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tq6bq450l0idg gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_t22b1ebhvur5a gm_l45;
    gm_tl572d7579hns gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_t3ujhu6qph7de gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_th2lqbo77pvsc gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tq6bq450l0idg gm_l54;
    gm_tftb1odc739t5 gm_l55;
    {
        {
            {
                {
                    {
                        void * t41 = gm_l0;
                        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_f_archive_info_gq6dkee((void *)&gm_l34, t41, t42);
                    }
                    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l34))[64] != 0);
                    if (!t43) {
                        goto gm_lb3;
                    }
                    {
                        memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l34), 64);
                    }
                    {
                        {
                            uint8_t t44 = (uint8_t)(((((uint8_t *)a2) + 16))[16] != 0);
                            if (!t44) {
                                goto gm_lb5;
                            }
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
                            gm_l4 = t45;
                            gm_b16_8 t46 = gm_l4;
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                            uint8_t t48 = (!gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                            if (t48) {
                                gm_b16_8 t49 = gm_l1;
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                                gm_b16_8 t52 = gm_l4;
                                gm_b16_8 t53;
                                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_se24c7hpp401n));
                                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(10));
                                gm_b16_8 t54 = gm_f_sha_mismatch_lq7bp8e((void *)NULL, cx, NULL, t49, t50, t51, t52, t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                                ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t54);
                                goto gm_fdone;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                    }
gm_lb4:;
                    {
                        gm_t0uuhaljsf410 t55 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&F.l3));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 0;
                        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t55);
                        goto gm_fdone;
                    }
                }
gm_lb3:;
            }
gm_lb2:;
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_tnpifv20irnph t57;
            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
            gm_f_format_s20j3pl((void *)&gm_l35, (void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcpsceljs9unl1, 625, 17);
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
            gm_l5 = t59;
            memset(&F.l6, 0, sizeof F.l6);
            {
                {
                    void * t60 = gm_l0;
                    uint8_t t61 = (uint8_t)(((((uint8_t *)t60) + 64))[40] != 0);
                    if (!t61) {
                        goto gm_lb7;
                    }
                    memmove(((uint8_t *)&F.l7), (((uint8_t *)t60) + 64), 40);
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                    gm_l8 = t62;
                    gm_l9 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t63 = gm_l9;
                            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t65 = (t63 < t64);
                            if (!t65) {
                                goto gm_lb8;
                            }
                            {
                                void * t66 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint64_t t68 = gm_l9;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l10), (((uint8_t *)t66) + (uintptr_t)t68 * 168), 168);
                                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
                                        gm_b16_8 t70 = gm_l5;
                                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                                        uint8_t t72 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t71)));
                                        if (t72) {
                                            gm_ts1nru0t7g7aj t73 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l10));
                                            GM_ST(uint8_t, (uint8_t *)&F.l6 + 169, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l6 + 170, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l6 + 172, 0);
                                            GM_ST(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l6), t73);
                                            ((uint8_t *)&F.l6)[168] = 1;
                                        }
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 628, 9);
                                }
gm_lb11:;
                            }
gm_lb10:;
                            uint64_t t74 = gm_l9;
                            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                            uint64_t t77 = r75;
                            gm_l9 = t77;
                        }
                    }
gm_lb8:;
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
        }
        {
            memset(&gm_l11, 0, sizeof gm_l11);
            {
                {
                    uint8_t t78 = (uint8_t)((((uint8_t *)&F.l6))[168] != 0);
                    if (!t78) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&F.l12), ((uint8_t *)&F.l6), 168);
                    {
                        {
                            uint8_t t79 = (uint8_t)(((((uint8_t *)&F.l12) + 112))[16] != 0);
                            if (!t79) {
                                goto gm_lb16;
                            }
                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 112));
                            gm_l13 = t80;
                            {
                                gm_b16_8 t81 = gm_l13;
                                gm_b16_8 t82;
                                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_son0j0k0t10ro));
                                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(7));
                                gm_f_strip_prefix_b4949s8((void *)&gm_l37, t81, t82);
                            }
                            uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l37))[16] != 0);
                            if (t83) {
                                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l37));
                                gm_l36 = t84;
                            } else {
                                gm_b16_8 t85 = gm_l13;
                                gm_l36 = t85;
                            }
                            {
                                gm_b16_8 t86 = gm_l36;
                                GM_ST(uint8_t, (uint8_t *)&gm_l11 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l11 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l11 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), t86);
                                ((uint8_t *)&gm_l11)[16] = 1;
                            }
                            goto gm_lb15;
                        }
gm_lb16:;
                    }
gm_lb15:;
                    goto gm_lb13;
                }
gm_lb14:;
            }
gm_lb13:;
            memset(&gm_l14, 0, sizeof gm_l14);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(0));
            gm_l15 = t87;
            {
                {
                    uint8_t t88 = (uint8_t)(((((uint8_t *)a2) + 16))[16] != 0);
                    if (!t88) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
                    gm_l16 = t89;
                    gm_b16_8 t90 = gm_l16;
                    GM_ST(uint8_t, (uint8_t *)&gm_l14 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l14 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l14 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l14), t90);
                    ((uint8_t *)&gm_l14)[16] = 1;
                    gm_b16_8 t91;
                    GM_ST(void *, (uint8_t *)&t91, (void *)(gm_se24c7hpp401n));
                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(10));
                    gm_l15 = t91;
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    {
                        uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                        if (!t92) {
                            goto gm_lb20;
                        }
                        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                        gm_l17 = t93;
                        gm_b16_8 t94 = gm_l17;
                        GM_ST(uint8_t, (uint8_t *)&gm_l14 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l14 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l14 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l14), t94);
                        ((uint8_t *)&gm_l14)[16] = 1;
                        gm_b16_8 t95;
                        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sgib96mf5meqv));
                        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(10));
                        gm_l15 = t95;
                        goto gm_lb19;
                    }
gm_lb20:;
                }
gm_lb19:;
            }
gm_lb17:;
            memset(&gm_l18, 0, sizeof gm_l18);
            {
                {
                    uint8_t t96 = (uint8_t)((((uint8_t *)&F.l6))[168] != 0);
                    if (!t96) {
                        goto gm_lb22;
                    }
                    memmove(((uint8_t *)&F.l19), ((uint8_t *)&F.l6), 168);
                    {
                        {
                            uint8_t t97 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                            if (!t97) {
                                goto gm_lb24;
                            }
                            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                            gm_l20 = t98;
                            {
                                gm_b16_8 t99 = gm_l20;
                                gm_tnpifv20irnph t100;
                                GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
                                gm_th2lqbo77pvsc t101 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 112));
                                gm_f_format_9di2rrs((void *)&gm_l38, (void *)NULL, cx, NULL, t100);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srcpsceljs9unl1, 650, 29);
                                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                                gm_th2lqbo77pvsc t105;
                                GM_ST(uint8_t, (uint8_t *)&t105 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t105 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t105 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t105), t104);
                                ((uint8_t *)&t105)[16] = 1;
                                uint8_t t106 = (gm_eq0ogca82g1f(((uint8_t *)&t101), ((uint8_t *)&t105)));
                                if (t106) {
                                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 136));
                                    GM_ST(uint8_t, (uint8_t *)&gm_l18 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&gm_l18 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&gm_l18 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l18), t107);
                                    ((uint8_t *)&gm_l18)[16] = 1;
                                }
                            }
                            goto gm_lb23;
                        }
gm_lb24:;
                    }
gm_lb23:;
                    goto gm_lb21;
                }
gm_lb22:;
            }
gm_lb21:;
        }
        {
            {
                {
                    void * t108 = gm_l0;
                    uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 1));
                    if (!t109) {
                        goto gm_lb26;
                    }
                    {
                        {
                            uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                            if (t110) {
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
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 653, 26);
                    }
gm_lb27:;
                    uint8_t t111 = gm_l39;
                    if (!t111) {
                        goto gm_lb26;
                    }
                    void * t112 = gm_l0;
                    {
                        {
                            uint8_t t113 = (uint8_t)(((((uint8_t *)t112) + 64))[40] != 0);
                            if (t113) {
                                goto gm_lb31;
                            }
                            gm_l40 = ((uint8_t)1);
                            goto gm_lb30;
                        }
gm_lb31:;
                        {
                            gm_l40 = ((uint8_t)0);
                            goto gm_lb30;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 654, 12);
                    }
gm_lb30:;
                    uint8_t t114 = gm_l40;
                    if (t114) {
                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)gm_clp0sh2cgr2));
                        memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                        ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t115);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t116 = gm_l1;
                        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_t6j6smj7qoctu t118;
                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t116);
                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 16), t117);
                        gm_f_format_cvqjb45((void *)&gm_l41, (void *)NULL, cx, NULL, &t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                    if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcpsceljs9unl1, 655, 17);
                    {
                        gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                        memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                        ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t121);
                        goto gm_fdone;
                    }
                }
gm_lb26:;
            }
gm_lb25:;
            {
                {
                    uint8_t t122 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                    if (!t122) {
                        goto gm_lb34;
                    }
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                    gm_l21 = t123;
                    void * t124 = gm_l0;
                    void * t125 = (void *)(((uint8_t *)t124) + 48);
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_b16_8 t127 = gm_l21;
                    gm_b16_8 t128 = gm_f_archive_dir_srvvp3g((void *)NULL, cx, NULL, t125, t126, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t128;
                    gm_b16_8 t129 = gm_l22;
                    uint8_t t130 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t130) {
                        gm_b16_8 t131 = gm_l22;
                        gm_b0_1 t132 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            gm_b16_8 t133 = gm_l1;
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                            gm_b16_8 t135 = gm_l21;
                            gm_b16_8 t136 = gm_l22;
                            gm_th2lqbo77pvsc t137 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l18));
                            gm_f_summarize_archive_cicsplu((void *)&gm_l42, (void *)NULL, cx, NULL, t133, t134, t135, t136, &t137);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (t138) {
                        } else {
                            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                            memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                            ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t139);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&F.l23), (((uint8_t *)&gm_l42) + 8), 64);
                        }
                        {
                            void * t140 = gm_l0;
                            void * t141 = (void *)(((uint8_t *)t140) + 232);
                            gm_tftb1odc739t5 t142 = gm_f_push_mt615h0(t141, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l23)));
                            gm_l43 = t142;
                        }
                        uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                        if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcpsceljs9unl1, 666, 13);
                        {
                            gm_t0uuhaljsf410 t144 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&F.l23));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 0;
                            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t144);
                            goto gm_fdone;
                        }
                    }
                    goto gm_lb33;
                }
gm_lb34:;
            }
gm_lb33:;
        }
        {
            void * t145 = gm_l0;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)t145) + 16));
            if (t146) {
                {
                    gm_b16_8 t147 = gm_l1;
                    gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_t6j6smj7qoctu t149;
                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t147);
                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 16), t148);
                    gm_f_format_g1540jg((void *)&gm_l44, (void *)NULL, cx, NULL, &t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srcpsceljs9unl1, 671, 17);
                {
                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                    memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                    ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t152);
                    goto gm_fdone;
                }
            }
            gm_b16_8 t153 = gm_l1;
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            if (gm_pf454qfrpt6r(cx, gm_s5ujsjbtr77bt, 13, gm_srcpsceljs9unl1, 673, 5)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t153) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t153)), GM_LD(uintptr_t, ((uint8_t *)&t153) + 8), gm_srcpsceljs9unl1, 673, 5))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_sp02qqgucmpt7, 2, gm_srcpsceljs9unl1, 673, 5)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t154) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t154)), GM_LD(uintptr_t, ((uint8_t *)&t154) + 8), gm_srcpsceljs9unl1, 673, 5))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_ssv5povjldd7c, 2, gm_srcpsceljs9unl1, 673, 5)) {
                goto gm_lad;
            }
            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            void * t156 = gm_l0;
            void * t157 = (void *)(((uint8_t *)t156) + 48);
            gm_b16_8 t158 = gm_f_downloads_vdenqu5((void *)NULL, cx, NULL, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_download_eek737p((void *)&gm_l45, (void *)NULL, cx, NULL, t155, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
            if (t159) {
            } else {
                gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t160);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l24), (((uint8_t *)&gm_l45) + 8), 32);
            {
                {
                    uint8_t t161 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                    if (!t161) {
                        goto gm_lb36;
                    }
                    gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                    gm_l25 = t162;
                    gm_b16_8 t163 = gm_l25;
                    gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                    uint8_t t165 = (!gm_eq0iqiubfm7e(((uint8_t *)&t163), ((uint8_t *)&t164)));
                    if (t165) {
                        gm_b16_8 t166 = gm_l1;
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                        gm_b16_8 t169 = gm_l25;
                        gm_b16_8 t170 = gm_l15;
                        gm_b16_8 t171 = gm_f_sha_mismatch_lq7bp8e((void *)NULL, cx, NULL, t166, t167, t168, t169, t170);
                        if (gm_unw.kind) {
                            goto gm_ld40_1;
                        }
                        memset(&F.ret, 0, sizeof F.ret);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t171);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld40_1;
                    }
                    goto gm_lb35;
                }
gm_lb36:;
            }
gm_lb35:;
            void * t172 = gm_l0;
            void * t173 = (void *)(((uint8_t *)t172) + 48);
            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
            gm_b16_8 t176 = gm_f_archive_dir_srvvp3g((void *)NULL, cx, NULL, t173, t174, t175);
            if (gm_unw.kind) {
                goto gm_ld40_1;
            }
            gm_l26 = t176;
            gm_b16_8 t177 = gm_l26;
            uint8_t t178 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t177);
            if (gm_unw.kind) {
                goto gm_ld40_1;
            }
            uint8_t t179 = (uint8_t)!t178;
            if (t179) {
                {
                    gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                    gm_b16_8 t181 = gm_l26;
                    gm_f_unpack_eb2aboi((void *)&gm_l46, (void *)NULL, cx, NULL, t180, t181);
                    if (gm_unw.kind) {
                        goto gm_ld40_1;
                    }
                }
                uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                if (t182) {
                } else {
                    gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                    gm_l27 = t183;
                    {
                        gm_b16_8 t184 = gm_l1;
                        gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t186 = gm_l27;
                        gm_tfeab279c16rr t187;
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t184);
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 16), t185);
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 32), t186);
                        gm_f_format_mv68p71((void *)&gm_l47, (void *)NULL, cx, NULL, &t187);
                        if (gm_unw.kind) {
                            goto gm_ld40_1;
                        }
                    }
                    uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcpsceljs9unl1, 681, 51);
                    {
                        gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                        gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                        memset(&F.ret, 0, sizeof F.ret);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t190);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld40_1;
                    }
                }
                {
                    gm_b16_8 t191 = gm_l26;
                    gm_f_tree_checksum_k6o2imm((void *)&gm_l48, (void *)NULL, cx, NULL, t191);
                    if (gm_unw.kind) {
                        goto gm_ld40_1;
                    }
                }
                uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                if (t192) {
                    gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                    gm_l29 = t193;
                } else {
                    gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                    gm_l28 = t194;
                    gm_b16_8 t195 = gm_l26;
                    gm_ttln0d0hd70en t196 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t195);
                    if (gm_unw.kind) {
                        goto gm_ld40_1;
                    }
                    {
                        gm_b16_8 t197 = gm_l1;
                        gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t199 = gm_l28;
                        gm_tfeab279c16rr t200;
                        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 0), t197);
                        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 16), t198);
                        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 32), t199);
                        gm_f_format_mv68p71((void *)&gm_l49, (void *)NULL, cx, NULL, &t200);
                        if (gm_unw.kind) {
                            goto gm_ld40_1;
                        }
                    }
                    uint8_t t201 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                    if (!t201) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_srcpsceljs9unl1, 684, 21);
                    {
                        gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                        gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                        memset(&F.ret, 0, sizeof F.ret);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t203);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld40_1;
                    }
                }
                {
                    {
                        uint8_t t204 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                        if (!t204) {
                            goto gm_lb38;
                        }
                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                        gm_l30 = t205;
                        gm_b16_8 t206 = gm_l30;
                        gm_b16_8 t207 = gm_l29;
                        uint8_t t208 = (!gm_eq0iqiubfm7e(((uint8_t *)&t206), ((uint8_t *)&t207)));
                        if (t208) {
                            gm_b16_8 t209 = gm_l26;
                            gm_ttln0d0hd70en t210 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t209);
                            if (gm_unw.kind) {
                                goto gm_ld40_1;
                            }
                            {
                                gm_b16_8 t211 = gm_l1;
                                gm_b16_8 t212 = gm_l30;
                                gm_b16_8 t213 = gm_l29;
                                gm_tfeab279c16rr t214;
                                GM_ST(gm_b16_8, ((uint8_t *)&t214 + 0), t211);
                                GM_ST(gm_b16_8, ((uint8_t *)&t214 + 16), t212);
                                GM_ST(gm_b16_8, ((uint8_t *)&t214 + 32), t213);
                                gm_f_format_vakr6l2((void *)&gm_l50, (void *)NULL, cx, NULL, &t214);
                                if (gm_unw.kind) {
                                    goto gm_ld40_1;
                                }
                            }
                            uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcpsceljs9unl1, 689, 25);
                            {
                                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                                gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                                memset(&F.ret, 0, sizeof F.ret);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t217);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld40_1;
                            }
                        }
                        goto gm_lb37;
                    }
gm_lb38:;
                }
gm_lb37:;
                gm_b16_8 t218 = gm_l26;
                gm_b0_1 t219 = gm_f_make_read_only_a29eok8((void *)NULL, cx, NULL, t218);
                if (gm_unw.kind) {
                    goto gm_ld40_1;
                }
                gm_b16_8 t220 = gm_l26;
                gm_b0_1 t221 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t220);
                if (gm_unw.kind) {
                    goto gm_ld40_1;
                }
                {
                    gm_b16_8 t222 = gm_l26;
                    gm_f_canonicalize_epo7cln((void *)&gm_l51, (void *)NULL, cx, NULL, t222);
                    if (gm_unw.kind) {
                        goto gm_ld40_1;
                    }
                }
                uint8_t t223 = (uint8_t)((((uint8_t *)&gm_l51))[16] != 0);
                if (t223) {
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                    gm_l31 = t224;
                } else {
                    {
                        gm_b16_8 t225 = gm_l26;
                        gm_tnpifv20irnph t226;
                        GM_ST(gm_b16_8, ((uint8_t *)&t226 + 0), t225);
                        gm_f_format_bpl33sh((void *)&gm_l52, (void *)NULL, cx, NULL, t226);
                        if (gm_unw.kind) {
                            goto gm_ld40_1;
                        }
                    }
                    uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                    if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcpsceljs9unl1, 694, 48);
                    {
                        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                        gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                        memset(&F.ret, 0, sizeof F.ret);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t229);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld40_1;
                    }
                }
                {
                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                    gm_b16_8 t232 = gm_l31;
                    gm_b16_8 t233 = gm_l29;
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l32 + 0), t230);
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l32 + 16), t231);
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l32 + 32), t232);
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l32 + 48), t233);
                }
                {
                    void * t234 = gm_l0;
                    void * t235 = (void *)(((uint8_t *)t234) + 232);
                    gm_tftb1odc739t5 t236 = gm_f_push_mt615h0(t235, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l32)));
                    gm_l53 = t236;
                }
                uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                if (!t237) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcpsceljs9unl1, 696, 9);
                {
                    gm_t0uuhaljsf410 t238 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&F.l32));
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    ((uint8_t *)&F.ret)[0] = 0;
                    GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&F.ret + 8), t238);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld40_1;
                }
            }
        }
        {
            gm_b16_8 t239 = gm_l26;
            gm_b0_1 t240 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t239);
            if (gm_unw.kind) {
                goto gm_ld40_1;
            }
            gm_b16_8 t241 = gm_l1;
            gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
            gm_b16_8 t244 = gm_l26;
            gm_th2lqbo77pvsc t245 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l18));
            gm_f_summarize_archive_cicsplu((void *)&gm_l54, (void *)NULL, cx, NULL, t241, t242, t243, t244, &t245);
            if (gm_unw.kind) {
                goto gm_ld40_1;
            }
            uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
            if (t246) {
            } else {
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                memset(&F.ret, 0, sizeof F.ret);
                ((uint8_t *)&F.ret)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t247);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                goto gm_ld40_1;
            }
            memmove(((uint8_t *)&F.l33), (((uint8_t *)&gm_l54) + 8), 64);
            void * t248 = gm_l0;
            void * t249 = (void *)(((uint8_t *)t248) + 232);
            gm_tftb1odc739t5 t250 = gm_f_push_mt615h0(t249, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l33)));
            gm_l55 = t250;
            uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
            if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcpsceljs9unl1, 701, 5);
            gm_t0uuhaljsf410 t252 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&F.l33));
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&F.ret + 8), t252);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld40_1;
        }
    }
    {
        gm_b0_1 t253 = gm_f_discard_bdpju80((void *)NULL, cx, NULL, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l24)));
    }
    goto gm_ls254;
gm_ld40_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b0_1 t255 = gm_f_discard_bdpju80((void *)NULL, cx, NULL, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l24)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls254:;
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
__attribute__((aligned(4))) void gm_f_ensure_checkout_hvmsso1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_ensure_checkout_hvmsso1 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* dest */
    gm_b16_8 gm_l6; /* dep */
    gm_b16_8 gm_l7; /* url */
    gm_b16_8 gm_l8; /* db_dir */
    uint8_t gm_l9; /* has */
    uint8_t gm_l10; /* has2 */
    gm_b16_8 gm_l11; /* e */
    gm_b16_8 gm_l12; /* sum */
    gm_b16_8 gm_l15; /* sum */
    gm_b16_8 gm_l16; /* canon */
    GM_STACK_CHECK(&F, 3056u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l17;
    gm_tqetrgo0q24b5 gm_l18;
    gm_trqfvmjtt4sij gm_l19;
    gm_b16_8 gm_l20;
    uint8_t gm_l21;
    gm_b16_8 gm_l22;
    gm_th2lqbo77pvsc gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tl572d7579hns gm_l25;
    uint8_t gm_l26;
    gm_b16_8 gm_l27;
    gm_th2lqbo77pvsc gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tl572d7579hns gm_l30;
    gm_t3ujhu6qph7de gm_l31;
    gm_tie69pa8pplgc gm_l32;
    gm_tabb710d9mq2e gm_l33;
    uint8_t gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    gm_tie69pa8pplgc gm_l36;
    gm_t3ujhu6qph7de gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_th2lqbo77pvsc gm_l39;
    gm_tabb710d9mq2e gm_l40;
    {
        {
            {
                {
                    void * t13 = gm_l0;
                    void * t14 = (void *)(((uint8_t *)t13) + 392);
                    gm_t6ok5tvcjc200 t15 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                    gm_f_get_vc34oj2((void *)&gm_l17, t14, &t15);
                }
                uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
                if (!t16) {
                    goto gm_lb2;
                }
                gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                gm_l3 = t17;
                {
                    gm_b16_8 t18 = gm_l3;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t18);
                    goto gm_fdone;
                }
            }
gm_lb2:;
        }
gm_lb1:;
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 48);
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_b16_8 t23 = gm_f_checkout_dir_mvijcuu((void *)NULL, cx, NULL, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)t24) + 112);
        gm_t6ok5tvcjc200 t26 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
        gm_f_get_9fn35sd((void *)&gm_l18, t25, &t26);
        uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l18))[40] != 0);
        if (!t27) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 523, 14);
        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 40);
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
        gm_l20 = t28;
        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l20));
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srcpsceljs9unl1, 523, 14);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 96), 96);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
        gm_l6 = t31;
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
        gm_l7 = t32;
        gm_b16_8 t33 = gm_l4;
        uint8_t t34 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!t34;
        if (t35) {
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 48);
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_b16_8 t39 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t39;
            gm_b16_8 t40 = gm_l8;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sbpcdsg6ifd91));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
            gm_b16_8 t42 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t43) {
                gm_b16_8 t44 = gm_l8;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_f_rev_parse_3l092b4((void *)&gm_l23, (void *)NULL, cx, NULL, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                if (t46) {
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                    gm_l22 = t47;
                } else {
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(0));
                    gm_l22 = t48;
                }
                gm_b16_8 t49 = gm_l22;
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                gm_l21 = t51;
            } else {
                gm_l21 = ((uint8_t)0);
            }
            uint8_t t52 = gm_l21;
            gm_l9 = t52;
            uint8_t t53 = gm_l9;
            uint8_t t54 = (uint8_t)!t53;
            if (t54) {
                void * t55 = gm_l0;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t55) + 16));
                if (t56) {
                    {
                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t58 = gm_f_short_rev_uin3i2s(t57);
                        gm_b16_8 t59 = gm_l6;
                        gm_b16_8 t60 = gm_l7;
                        gm_tfeab279c16rr t61;
                        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t58);
                        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 16), t59);
                        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 32), t60);
                        gm_f_format_nsoplg3((void *)&gm_l24, (void *)NULL, cx, NULL, &t61);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcpsceljs9unl1, 531, 25);
                    {
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t64);
                        goto gm_fdone;
                    }
                }
                {
                    void * t65 = gm_l0;
                    gm_t6ok5tvcjc200 t66 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                    gm_f_mirror_8u3u6oe((void *)&gm_l25, (void *)NULL, cx, NULL, t65, &t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (t67) {
                } else {
                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t68);
                    goto gm_fdone;
                }
                gm_b16_8 t69 = gm_l8;
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sbpcdsg6ifd91));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
                gm_b16_8 t71 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t72 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t72) {
                    gm_b16_8 t73 = gm_l8;
                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_f_rev_parse_3l092b4((void *)&gm_l28, (void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l28))[16] != 0);
                    if (t75) {
                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                        gm_l27 = t76;
                    } else {
                        gm_b16_8 t77;
                        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(0));
                        gm_l27 = t77;
                    }
                    gm_b16_8 t78 = gm_l27;
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    uint8_t t80 = (gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
                    gm_l26 = t80;
                } else {
                    gm_l26 = ((uint8_t)0);
                }
                uint8_t t81 = gm_l26;
                gm_l10 = t81;
                uint8_t t82 = gm_l10;
                uint8_t t83 = (uint8_t)!t82;
                if (t83) {
                    {
                        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t85 = gm_l6;
                        gm_b16_8 t86 = gm_l7;
                        gm_b16_8 t87 = gm_l6;
                        gm_t0uuhaljsf410 t88;
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t84);
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 16), t85);
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 32), t86);
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 48), t87);
                        gm_f_format_2orhd15((void *)&gm_l29, (void *)NULL, cx, NULL, &t88);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcpsceljs9unl1, 541, 25);
                    {
                        gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t91);
                        goto gm_fdone;
                    }
                }
            }
            {
                gm_b16_8 t92 = gm_l8;
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t94 = gm_l4;
                gm_f_checkout_j42th18((void *)&gm_l30, (void *)NULL, cx, NULL, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (t95) {
            } else {
                gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t96);
                goto gm_fdone;
            }
            {
                gm_b16_8 t97 = gm_l4;
                gm_f_tree_checksum_k6o2imm((void *)&gm_l31, (void *)NULL, cx, NULL, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (t98) {
                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_l12 = t99;
            } else {
                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_l11 = t100;
                gm_b16_8 t101 = gm_l4;
                gm_ttln0d0hd70en t102 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t103 = gm_l11;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t103);
                goto gm_fdone;
            }
            {
                {
                    {
                        void * t104 = gm_l0;
                        gm_t6ok5tvcjc200 t105 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                        gm_f_locked_entry_4693mpc((void *)&gm_l32, t104, &t105);
                    }
                    uint8_t t106 = (uint8_t)((((uint8_t *)&gm_l32))[168] != 0);
                    if (!t106) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l32), 168);
                    }
                    {
                        {
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 96));
                            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                            uint8_t t109 = (gm_eq0iqiubfm7e(((uint8_t *)&t107), ((uint8_t *)&t108)));
                            if (!t109) {
                                goto gm_lb6;
                            }
                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 136));
                            gm_b16_8 t111 = gm_l12;
                            uint8_t t112 = (!gm_eq0iqiubfm7e(((uint8_t *)&t110), ((uint8_t *)&t111)));
                            if (!t112) {
                                goto gm_lb6;
                            }
                            gm_b16_8 t113 = gm_l4;
                            gm_ttln0d0hd70en t114 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t113);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 136));
                                gm_b16_8 t117 = gm_l12;
                                gm_tfeab279c16rr t118;
                                GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t115);
                                GM_ST(gm_b16_8, ((uint8_t *)&t118 + 16), t116);
                                GM_ST(gm_b16_8, ((uint8_t *)&t118 + 32), t117);
                                gm_f_format_vakr6l2((void *)&gm_l33, (void *)NULL, cx, NULL, &t118);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                            if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcpsceljs9unl1, 558, 25);
                            {
                                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t121);
                                goto gm_fdone;
                            }
                        }
gm_lb6:;
                    }
gm_lb5:;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                void * t122 = gm_l0;
                void * t123 = (void *)(((uint8_t *)t122) + 432);
                gm_b16_8 t124 = gm_l12;
                gm_b0_1 t125 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t123, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t126 = gm_l4;
            gm_b0_1 t127 = gm_f_make_read_only_a29eok8((void *)NULL, cx, NULL, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            {
                void * t128 = gm_l0;
                void * t129 = (void *)(((uint8_t *)t128) + 432);
                gm_t6ok5tvcjc200 t130 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_vc34oj2((void *)&gm_l35, t129, &t130);
            }
            {
                {
                    uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
                    if (t131) {
                        goto gm_lb8;
                    }
                    gm_l34 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l34 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 563, 15);
            }
gm_lb7:;
            uint8_t t132 = gm_l34;
            if (t132) {
                {
                    {
                        {
                            void * t133 = gm_l0;
                            gm_t6ok5tvcjc200 t134 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                            gm_f_locked_entry_4693mpc((void *)&gm_l36, t133, &t134);
                        }
                        uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l36))[168] != 0);
                        if (!t135) {
                            goto gm_lb11;
                        }
                        {
                            memmove(((uint8_t *)&F.l14), ((uint8_t *)&gm_l36), 168);
                        }
                        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 96));
                        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        uint8_t t138 = (gm_eq0iqiubfm7e(((uint8_t *)&t136), ((uint8_t *)&t137)));
                        if (t138) {
                            {
                                gm_b16_8 t139 = gm_l4;
                                gm_f_tree_checksum_k6o2imm((void *)&gm_l37, (void *)NULL, cx, NULL, t139);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                            if (t140) {
                            } else {
                                gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t141);
                                goto gm_fdone;
                            }
                            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                            gm_l15 = t142;
                            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 136));
                            gm_b16_8 t144 = gm_l15;
                            uint8_t t145 = (!gm_eq0iqiubfm7e(((uint8_t *)&t143), ((uint8_t *)&t144)));
                            if (t145) {
                                {
                                    gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                    gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 136));
                                    gm_b16_8 t148 = gm_l15;
                                    gm_b16_8 t149 = gm_l4;
                                    gm_t0uuhaljsf410 t150;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t146);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 16), t147);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 32), t148);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 48), t149);
                                    gm_f_format_jth3mgf((void *)&gm_l38, (void *)NULL, cx, NULL, &t150);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srcpsceljs9unl1, 569, 29);
                                {
                                    gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                    gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t153);
                                    goto gm_fdone;
                                }
                            }
                            {
                                void * t154 = gm_l0;
                                void * t155 = (void *)(((uint8_t *)t154) + 432);
                                gm_b16_8 t156 = gm_l15;
                                gm_b0_1 t157 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t155, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t156);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                        }
                        goto gm_lb10;
                    }
gm_lb11:;
                }
gm_lb10:;
            }
        }
    }
    {
        gm_b16_8 t158 = gm_l4;
        gm_b0_1 t159 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t160 = gm_l4;
        gm_f_canonicalize_epo7cln((void *)&gm_l39, (void *)NULL, cx, NULL, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t161 = (uint8_t)((((uint8_t *)&gm_l39))[16] != 0);
        if (t161) {
            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
            gm_l16 = t162;
        } else {
            {
                gm_b16_8 t163 = gm_l4;
                gm_tnpifv20irnph t164;
                GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t163);
                gm_f_format_bpl33sh((void *)&gm_l40, (void *)NULL, cx, NULL, t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
            if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcpsceljs9unl1, 582, 46);
            {
                gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t167);
                goto gm_fdone;
            }
        }
        void * t168 = gm_l0;
        void * t169 = (void *)(((uint8_t *)t168) + 392);
        gm_b16_8 t170 = gm_l16;
        gm_b0_1 t171 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t169, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t172 = gm_l16;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t172);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_entry_name_qnlemsm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_entry_name_qnlemsm F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l8;
    gm_tqetrgo0q24b5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_b16_8 gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_th2lqbo77pvsc gm_l13;
    {
        {
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 472);
                gm_t6ok5tvcjc200 t13 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_vc34oj2((void *)&gm_l8, t12, &t13);
            }
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_l2 = t15;
            gm_b16_8 t16 = gm_l2;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_epvducg((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t17;
    }
    {
        {
            {
                void * t18 = gm_l0;
                void * t19 = (void *)(((uint8_t *)t18) + 112);
                gm_t6ok5tvcjc200 t20 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_9fn35sd((void *)&gm_l9, t19, &t20);
            }
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l9))[40] != 0);
            if (!t21) {
                goto gm_lb4;
            }
            {
                memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l9), 40);
            }
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_l5 = t22;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t25 = (t23 < t24);
                    if (!t25) {
                        goto gm_lb5;
                    }
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t28 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)t26) + (uintptr_t)t28 * 96), 96);
                                void * t29 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                                gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t29, t30);
                                gm_l10 = t31;
                                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcpsceljs9unl1, 398, 31);
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 398, 9);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_f_sort_skeree7(t37);
    void * t38 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t40 = (((uint64_t)0ULL) < t39);
    if (t40) {
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t38) + (uintptr_t)((uint64_t)0ULL) * 16));
        GM_ST(uint8_t, (uint8_t *)&gm_l12 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l12 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l12 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l12), t41);
        ((uint8_t *)&gm_l12)[16] = 1;
    } else {
        memset(&gm_l12, 0, sizeof gm_l12);
    }
    {
        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l12), 24);
    }
    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
    if (t42) {
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
        gm_l11 = t43;
    } else {
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_snqvo5bgc2fch));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
        gm_l11 = t44;
    }
    gm_b16_8 t45 = gm_l11;
    F.ret = t45;
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
inline __attribute__((aligned(4))) void gm_f_find_ref_h89e731(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_ref_h89e731 F;
    gm_b16_8 gm_l0; /* refs */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* r */
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
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 136, 5);
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
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_find_ref_h89e731_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_find_ref_h89e731(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s_c(gm_toq2f2q4opoa3 a0) { return gm_f_is_default_p4tis8s(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_is_refreshed_ifi7dst(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_is_refreshed_ifi7dst F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* names */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* n */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 680u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l11;
    gm_tqetrgo0q24b5 gm_l12;
    gm_tie69pa8pplgc gm_l13;
    void * t18 = gm_l0;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 24) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_l11 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t18) + 24) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb3;
            }
            gm_l11 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t18) + 24) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 24) + 8));
            gm_l2 = t25;
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 112);
                gm_t6ok5tvcjc200 t28 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                gm_f_get_9fn35sd((void *)&gm_l12, t27, &t28);
            }
            uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l12))[40] != 0);
            if (!t29) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 448, 21);
            {
                memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l12), 40);
            }
            gm_b16_8 t30 = gm_l2;
            gm_l4 = t30;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t31 = gm_l5;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t33 = (t31 < t32);
                    if (!t33) {
                        goto gm_lb5;
                    }
                    {
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t36 = gm_l5;
                        {
                            {
                                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t36 * 16));
                                gm_l6 = t37;
                                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                gm_l7 = t38;
                                gm_l8 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t39 = gm_l8;
                                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint8_t t41 = (t39 < t40);
                                        if (!t41) {
                                            goto gm_lb10;
                                        }
                                        {
                                            void * t42 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                            uint64_t t44 = gm_l8;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l9), (((uint8_t *)t42) + (uintptr_t)t44 * 96), 96);
                                                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                                                    gm_b16_8 t46 = gm_l6;
                                                    uint8_t t47 = (gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
                                                    if (t47) {
                                                        F.ret = ((uint8_t)1);
                                                        goto gm_fret;
                                                    }
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 450, 17);
                                            }
gm_lb13:;
                                        }
gm_lb12:;
                                        uint64_t t48 = gm_l8;
                                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                                        uint64_t t51 = r49;
                                        gm_l8 = t51;
                                    }
                                }
gm_lb10:;
                                {
                                    {
                                        {
                                            void * t52 = gm_l0;
                                            gm_t6ok5tvcjc200 t53 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
                                            gm_f_locked_entry_4693mpc((void *)&gm_l13, t52, &t53);
                                        }
                                        uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l13))[168] != 0);
                                        if (!t54) {
                                            goto gm_lb16;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l13), 168);
                                        }
                                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t56 = gm_l6;
                                        uint8_t t57 = (gm_eq0iqiubfm7e(((uint8_t *)&t55), ((uint8_t *)&t56)));
                                        if (t57) {
                                            F.ret = ((uint8_t)1);
                                            goto gm_fret;
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 449, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t58 = gm_l5;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l5 = t61;
                }
            }
gm_lb5:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 444, 12);
    }
gm_lb1:;
    uint8_t t62 = gm_l11;
    F.ret = t62;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf_c(gm_t6ok5tvcjc200 a0, gm_t6ok5tvcjc200 a1) { return gm_f_key_equal_n71njnf(&a0, &a1); }
extern __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1);
extern __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_locked_entry_4693mpc_c(void * a0, gm_t6ok5tvcjc200 a1) { gm_tie69pa8pplgc gm_r; gm_f_locked_entry_4693mpc(&gm_r, a0, &a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_mirror_8u3u6oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_mirror_8u3u6oe F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* u */
    gm_b16_8 gm_l6; /* db_dir */
    GM_STACK_CHECK(&F, 983u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqetrgo0q24b5 gm_l8;
    gm_trqfvmjtt4sij gm_l9;
    gm_b16_8 gm_l10;
    gm_tl572d7579hns gm_l11;
    gm_tv61mo81rn7eb gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 152));
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
                        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l4 = t15;
                        gm_b16_8 t16 = gm_l4;
                        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)a1));
                        uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
                        if (t18) {
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 430, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
        }
    }
gm_lb1:;
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 112);
        gm_t6ok5tvcjc200 t25 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)a1));
        gm_f_get_9fn35sd((void *)&gm_l8, t24, &t25);
    }
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[40] != 0);
    if (!t26) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 433, 14);
    {
        memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l8), 40);
    }
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
    gm_l10 = t27;
    {
        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l10));
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t29, gm_srcpsceljs9unl1, 433, 14);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t28) + (uintptr_t)((uint64_t)0ULL) * 96), 96);
    }
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
    if (gm_pf454qfrpt6r(cx, gm_s5ujsjbtr77bt, 13, gm_srcpsceljs9unl1, 434, 5)) {
        goto gm_lad;
    }
    if ((GM_LD(uintptr_t, ((uint8_t *)&t30) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t30)), GM_LD(uintptr_t, ((uint8_t *)&t30) + 8), gm_srcpsceljs9unl1, 434, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_sp02qqgucmpt7, 2, gm_srcpsceljs9unl1, 434, 5)) {
        goto gm_lad;
    }
    if ((GM_LD(uintptr_t, ((uint8_t *)&t31) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t31)), GM_LD(uintptr_t, ((uint8_t *)&t31) + 8), gm_srcpsceljs9unl1, 434, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_ssv5povjldd7c, 2, gm_srcpsceljs9unl1, 434, 5)) {
        goto gm_lad;
    }
    void * t32 = gm_l0;
    void * t33 = (void *)(((uint8_t *)t32) + 48);
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_b16_8 t35 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t33, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t35;
    {
        gm_b16_8 t36 = gm_l6;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
        gm_f_fetch_mirror_q06gvv0((void *)&gm_l11, (void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t38) {
    } else {
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t39);
        goto gm_fdone;
    }
    {
        gm_b16_8 t40 = gm_l6;
        gm_f_list_refs_j3is1o5((void *)&gm_l12, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t41) {
    } else {
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t42);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l12) + 8), 104);
    }
    void * t43 = gm_l0;
    void * t44 = (void *)(((uint8_t *)t43) + 152);
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
    gm_l13 = t46;
    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcpsceljs9unl1, 438, 5);
    {
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 192);
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_tmf08dt0mom0q t51 = GM_LD(gm_tmf08dt0mom0q, ((uint8_t *)&F.l7));
        gm_t3rj5o15e922s t52;
        GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t50);
        GM_ST(gm_tmf08dt0mom0q, ((uint8_t *)&t52 + 16), t51);
        gm_tftb1odc739t5 t53 = gm_f_push_r6o112l(t49, &t52);
        gm_l14 = t53;
    }
    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcpsceljs9unl1, 439, 5);
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
__attribute__((aligned(4))) void gm_f_pick_ronijl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_pick_ronijl9 F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* all */
    void * gm_l2; /* refs */
    gm_b16_8 gm_l3; /* db_dir */
    gm_b16_8 gm_l4; /* reqs */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l9; /* dup */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* conflict */
    gm_b16_8 gm_l20; /* t */
    gm_b16_8 gm_l21; /* rev */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l26; /* b */
    gm_b16_8 gm_l27; /* rev */
    gm_b16_8 gm_l28; /* r */
    gm_b16_8 gm_l29; /* rev */
    gm_b16_8 gm_l31; /* best_name */
    gm_b16_8 gm_l32; /* best_rev */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_t6j6smj7qoctu gm_l35; /* t */
    uint8_t gm_l37; /* ok */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l43; /* rev */
    GM_STACK_CHECK(&F, 3712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l44;
    uint8_t gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tabb710d9mq2e gm_l48;
    gm_th2lqbo77pvsc gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tom1ad5bcqgi1 gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_th2lqbo77pvsc gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_th2lqbo77pvsc gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tom1ad5bcqgi1 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_tabb710d9mq2e gm_l62;
    {
        gm_b16_8 t44 = gm_l1;
        gm_b16_8 t45 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t45;
        gm_trqfvmjtt4sij t46;
        gm_f_list_vsi9vun((void *)&t46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t46;
        gm_b16_8 t47 = gm_l4;
        gm_l6 = t47;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t48 = gm_l7;
                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t50 = (t48 < t49);
                if (!t50) {
                    goto gm_lb1;
                }
                {
                    void * t51 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t53 = gm_l7;
                    {
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t51) + (uintptr_t)t53 * 96), 96);
                            {
                                {
                                    uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 48) + 0));
                                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                                    if (!t55) {
                                        goto gm_lb7;
                                    }
                                    gm_l45 = ((uint8_t)1);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    gm_l45 = ((uint8_t)0);
                                    goto gm_lb6;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 147, 12);
                            }
gm_lb6:;
                            uint8_t t56 = gm_l45;
                            if (t56) {
                                gm_l44 = ((uint8_t)1);
                            } else {
                                uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 48) + 0));
                                uint8_t t58 = (uint8_t)((t57 == ((uint8_t)0ULL)));
                                gm_toq2f2q4opoa3 t59 = GM_LD(gm_toq2f2q4opoa3, (((uint8_t *)&F.l8) + 48));
                                gm_toq2f2q4opoa3 t60;
                                memset(&t60, 0, sizeof t60);
                                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
                                gm_l44 = t58;
                            }
                            uint8_t t61 = gm_l44;
                            if (t61) {
                                goto gm_lb3;
                            }
                            gm_l9 = ((uint8_t)0);
                            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            gm_l10 = t62;
                            gm_l11 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t63 = gm_l11;
                                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint8_t t65 = (t63 < t64);
                                    if (!t65) {
                                        goto gm_lb9;
                                    }
                                    {
                                        void * t66 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint64_t t68 = gm_l11;
                                        {
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l12), (((uint8_t *)t66) + (uintptr_t)t68 * 96), 96);
                                                }
                                                {
                                                    uint8_t t69 = gm_f_spec_equal_foa1umn(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l12) + 48)), &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l8) + 48)));
                                                    if (t69) {
                                                        gm_l9 = ((uint8_t)1);
                                                    }
                                                }
                                                goto gm_lb12;
                                            }
gm_lb13:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 149, 9);
                                        }
gm_lb12:;
                                    }
gm_lb11:;
                                    uint64_t t70 = gm_l11;
                                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                    uint64_t t73 = r71;
                                    gm_l11 = t73;
                                }
                            }
gm_lb9:;
                            uint8_t t74 = gm_l9;
                            uint8_t t75 = (uint8_t)!t74;
                            if (t75) {
                                void * t76 = (void *)((uint8_t *)&F.l5);
                                gm_tftb1odc739t5 t77 = gm_f_push_vgb8k63(t76, &(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l8)));
                                gm_l46 = t77;
                                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcpsceljs9unl1, 152, 22);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 146, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t79 = gm_l7;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                uint64_t t82 = r80;
                gm_l7 = t82;
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t83;
        gm_f_list_1q70459((void *)&t83, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t83;
        gm_b16_8 t84 = gm_l4;
        gm_l14 = t84;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t85 = gm_l15;
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t87 = (t85 < t86);
                if (!t87) {
                    goto gm_lb14;
                }
                {
                    void * t88 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t90 = gm_l15;
                    {
                        {
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t88) + (uintptr_t)t90 * 96), 96);
                            {
                                {
                                    uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 48) + 0));
                                    uint8_t t92 = (uint8_t)(t91 == ((uint8_t)1ULL));
                                    if (!t92) {
                                        goto gm_lb20;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l17), ((((uint8_t *)&F.l16) + 48) + 8), 40);
                                    }
                                    {
                                        void * t93 = (void *)((uint8_t *)&F.l13);
                                        gm_tftb1odc739t5 t94 = gm_f_push_ljifgtt(t93, &(*(gm_tin97tt4ud0s1 *)((uint8_t *)&F.l17)));
                                        gm_l47 = t94;
                                    }
                                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                    if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcpsceljs9unl1, 156, 36);
                                    goto gm_lb19;
                                }
gm_lb20:;
                            }
gm_lb19:;
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 155, 5);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t96 = gm_l15;
                uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                uint64_t t99 = r97;
                gm_l15 = t99;
            }
        }
gm_lb14:;
        gm_b16_8 t100 = gm_l0;
        gm_b16_8 t101 = gm_l4;
        gm_b16_8 t102 = gm_f_describe_mp0u2mq((void *)NULL, cx, NULL, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t103;
        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t100);
        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 16), t102);
        gm_f_format_n8h20c1((void *)&gm_l48, (void *)NULL, cx, NULL, &t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srcpsceljs9unl1, 158, 17);
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
        gm_l18 = t105;
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
        uint8_t t107 = (t106 > ((uint64_t)1ULL));
        if (t107) {
            gm_b16_8 t108 = gm_l18;
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t109);
            goto gm_fdone;
        }
        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
        uint8_t t111 = (t110 > ((uint64_t)0ULL));
        if (t111) {
            void * t112 = GM_LD(void *, ((uint8_t *)&F.l5));
            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t113, gm_srcpsceljs9unl1, 161, 16);
            memmove(((uint8_t *)&F.l19), (((uint8_t *)t112) + (uintptr_t)((uint64_t)0ULL) * 96), 96);
            {
                {
                    uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)2ULL));
                    if (!t115) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
                    gm_l20 = t116;
                    {
                        void * t117 = gm_l2;
                        gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)t117));
                        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                        gm_b16_8 t120 = gm_l20;
                        gm_f_find_ref_h89e731((void *)&gm_l49, t119, t120);
                    }
                    uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l49))[16] != 0);
                    if (t121) {
                        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l49));
                        gm_l21 = t122;
                    } else {
                        {
                            gm_b16_8 t123 = gm_l0;
                            gm_b16_8 t124 = gm_l20;
                            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                            gm_tfeab279c16rr t126;
                            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t123);
                            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 16), t124);
                            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 32), t125);
                            gm_f_format_l821hfj((void *)&gm_l50, (void *)NULL, cx, NULL, &t126);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                        if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcpsceljs9unl1, 165, 29);
                        {
                            gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t129);
                            goto gm_fdone;
                        }
                    }
                    uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                    uint8_t t131 = (t130 > ((uint64_t)0ULL));
                    if (t131) {
                        gm_b16_8 t132 = gm_l20;
                        gm_f_tag_version_aloeltn((void *)&gm_l51, (void *)NULL, cx, NULL, t132);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l51))[56] != 0);
                        if (t133) {
                            memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l51), 56);
                        } else {
                            gm_b16_8 t134 = gm_l18;
                            gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t135);
                            goto gm_fdone;
                        }
                        gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                        gm_l23 = t136;
                        gm_l24 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t137 = gm_l24;
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                uint8_t t139 = (t137 < t138);
                                if (!t139) {
                                    goto gm_lb23;
                                }
                                {
                                    void * t140 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                    uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                    uint64_t t142 = gm_l24;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l25), (((uint8_t *)t140) + (uintptr_t)t142 * 40), 40);
                                            }
                                            {
                                                void * t143 = (void *)((uint8_t *)&F.l25);
                                                uint8_t t144 = gm_f_req_matches_dflcrek(t143, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l22)));
                                                uint8_t t145 = (uint8_t)!t144;
                                                if (t145) {
                                                    gm_b16_8 t146 = gm_l18;
                                                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                                                    memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                                                    ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t147);
                                                    goto gm_fdone;
                                                }
                                            }
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 169, 21);
                                    }
gm_lb26:;
                                }
gm_lb25:;
                                uint64_t t148 = gm_l24;
                                uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                                uint64_t t151 = r149;
                                gm_l24 = t151;
                            }
                        }
gm_lb23:;
                    }
                    {
                        gm_b16_8 t152 = gm_l21;
                        gm_th2lqbo77pvsc t153;
                        memset(&t153, 0, sizeof t153);
                        gm_b16_8 t154 = gm_l20;
                        gm_tnpifv20irnph t155;
                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t154);
                        gm_f_format_7sll6e8((void *)&gm_l52, (void *)NULL, cx, NULL, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                        if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcpsceljs9unl1, 173, 67);
                        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                        gm_t8qqrlhb610a2 t159;
                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 0), t152);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t159 + 16), t153);
                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 40), t158);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
                        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t159);
                        goto gm_fdone;
                    }
                }
gm_lb22:;
                {
                    uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
                    uint8_t t161 = (uint8_t)(t160 == ((uint8_t)3ULL));
                    if (!t161) {
                        goto gm_lb28;
                    }
                    gm_b16_8 t162 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
                    gm_l26 = t162;
                    {
                        void * t163 = gm_l2;
                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)t163) + 40));
                        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                        gm_b16_8 t166 = gm_l26;
                        gm_f_find_ref_h89e731((void *)&gm_l53, t165, t166);
                    }
                    uint8_t t167 = (uint8_t)((((uint8_t *)&gm_l53))[16] != 0);
                    if (t167) {
                        gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l53));
                        gm_l27 = t168;
                    } else {
                        {
                            gm_b16_8 t169 = gm_l0;
                            gm_b16_8 t170 = gm_l26;
                            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                            gm_tfeab279c16rr t172;
                            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t169);
                            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t170);
                            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 32), t171);
                            gm_f_format_3pk07bs((void *)&gm_l54, (void *)NULL, cx, NULL, &t172);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                        if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srcpsceljs9unl1, 177, 29);
                        {
                            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                            gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t175);
                            goto gm_fdone;
                        }
                    }
                    uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                    uint8_t t177 = (t176 > ((uint64_t)0ULL));
                    if (t177) {
                        gm_b16_8 t178 = gm_l18;
                        gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                        memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t179);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t180 = gm_l27;
                        gm_th2lqbo77pvsc t181;
                        memset(&t181, 0, sizeof t181);
                        gm_b16_8 t182 = gm_l26;
                        gm_tnpifv20irnph t183;
                        GM_ST(gm_b16_8, ((uint8_t *)&t183 + 0), t182);
                        gm_f_format_asjuqmr((void *)&gm_l55, (void *)NULL, cx, NULL, t183);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t184 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                        if (!t184) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_srcpsceljs9unl1, 180, 67);
                        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                        gm_t8qqrlhb610a2 t187;
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t180);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t187 + 16), t181);
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 40), t186);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
                        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t187);
                        goto gm_fdone;
                    }
                }
gm_lb28:;
                {
                    uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
                    uint8_t t189 = (uint8_t)(t188 == ((uint8_t)4ULL));
                    if (!t189) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
                    gm_l28 = t190;
                    {
                        gm_b16_8 t191 = gm_l3;
                        gm_b16_8 t192 = gm_l28;
                        gm_f_rev_parse_3l092b4((void *)&gm_l56, (void *)NULL, cx, NULL, t191, t192);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t193 = (uint8_t)((((uint8_t *)&gm_l56))[16] != 0);
                    if (t193) {
                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                        gm_l29 = t194;
                    } else {
                        {
                            gm_b16_8 t195 = gm_l0;
                            gm_b16_8 t196 = gm_l28;
                            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                            gm_tfeab279c16rr t198;
                            GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t195);
                            GM_ST(gm_b16_8, ((uint8_t *)&t198 + 16), t196);
                            GM_ST(gm_b16_8, ((uint8_t *)&t198 + 32), t197);
                            gm_f_format_rpqj2mb((void *)&gm_l57, (void *)NULL, cx, NULL, &t198);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                        if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srcpsceljs9unl1, 184, 29);
                        {
                            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                            memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                            ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t201);
                            goto gm_fdone;
                        }
                    }
                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                    uint8_t t203 = (t202 > ((uint64_t)0ULL));
                    if (t203) {
                        gm_b16_8 t204 = gm_l18;
                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                        memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t205);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t206 = gm_l29;
                        gm_th2lqbo77pvsc t207;
                        memset(&t207, 0, sizeof t207);
                        gm_b16_8 t208 = gm_l28;
                        gm_tnpifv20irnph t209;
                        GM_ST(gm_b16_8, ((uint8_t *)&t209 + 0), t208);
                        gm_f_format_m5mroph((void *)&gm_l58, (void *)NULL, cx, NULL, t209);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                        if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srcpsceljs9unl1, 187, 67);
                        gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                        gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
                        gm_t8qqrlhb610a2 t213;
                        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t206);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t213 + 16), t207);
                        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 40), t212);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
                        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t213);
                        goto gm_fdone;
                    }
                }
gm_lb29:;
                {
                    goto gm_lb21;
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 162, 9);
            }
gm_lb21:;
        }
    }
    {
        uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
        uint8_t t215 = (t214 > ((uint64_t)0ULL));
        if (t215) {
            {
                memset(&F.l30, 0, sizeof F.l30);
            }
            gm_b16_8 t216;
            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(0));
            gm_l31 = t216;
            gm_b16_8 t217;
            GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(0));
            gm_l32 = t217;
            void * t218 = gm_l2;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)t218));
            gm_l33 = t219;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t220 = gm_l34;
                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t222 = (t220 < t221);
                    if (!t222) {
                        goto gm_lb31;
                    }
                    {
                        void * t223 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t225 = gm_l34;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l35), (((uint8_t *)t223) + (uintptr_t)t225 * 32), 32);
                                }
                                {
                                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
                                    gm_f_tag_version_aloeltn((void *)&gm_l59, (void *)NULL, cx, NULL, t226);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t227 = (uint8_t)((((uint8_t *)&gm_l59))[56] != 0);
                                if (t227) {
                                    memmove(((uint8_t *)&F.l36), ((uint8_t *)&gm_l59), 56);
                                } else {
                                    goto gm_lb33;
                                }
                                gm_l37 = ((uint8_t)1);
                                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                gm_l38 = t228;
                                gm_l39 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t229 = gm_l39;
                                        uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                        uint8_t t231 = (t229 < t230);
                                        if (!t231) {
                                            goto gm_lb36;
                                        }
                                        {
                                            void * t232 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                            uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint64_t t234 = gm_l39;
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l40), (((uint8_t *)t232) + (uintptr_t)t234 * 40), 40);
                                                    }
                                                    {
                                                        void * t235 = (void *)((uint8_t *)&F.l40);
                                                        uint8_t t236 = gm_f_req_matches_dflcrek(t235, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l36)));
                                                        uint8_t t237 = (uint8_t)!t236;
                                                        if (t237) {
                                                            gm_l37 = ((uint8_t)0);
                                                        }
                                                    }
                                                    goto gm_lb39;
                                                }
gm_lb40:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 199, 13);
                                            }
gm_lb39:;
                                        }
gm_lb38:;
                                        uint64_t t238 = gm_l39;
                                        uint64_t r239; bool o240 = __builtin_add_overflow(t238, ((uint64_t)1ULL), &r239);
                                        uint64_t t241 = r239;
                                        gm_l39 = t241;
                                    }
                                }
gm_lb36:;
                                uint8_t t242 = gm_l37;
                                uint8_t t243 = (uint8_t)!t242;
                                if (t243) {
                                    goto gm_lb33;
                                }
                                {
                                    {
                                        uint8_t t244 = (uint8_t)((((uint8_t *)&F.l30))[56] != 0);
                                        if (!t244) {
                                            goto gm_lb42;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l41), ((uint8_t *)&F.l30), 56);
                                        }
                                        {
                                            int32_t t245 = gm_f_cmp_version_0oibvro(&(*(gm_tu1089hhijadf *)((uint8_t *)&F.l36)), &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l41)));
                                            uint8_t t246 = (t245 < ((int32_t)0LL));
                                            if (t246) {
                                                goto gm_lb33;
                                            }
                                        }
                                        goto gm_lb41;
                                    }
gm_lb42:;
                                }
gm_lb41:;
                                {
                                    gm_tu1089hhijadf t247 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l36));
                                    GM_ST(uint8_t, (uint8_t *)&F.l30 + 57, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.l30 + 58, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.l30 + 60, 0);
                                    GM_ST(gm_tu1089hhijadf, ((uint8_t *)&F.l30), t247);
                                    ((uint8_t *)&F.l30)[56] = 1;
                                }
                                gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
                                gm_l31 = t248;
                                gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 16));
                                gm_l32 = t249;
                                goto gm_lb34;
                            }
gm_lb35:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 196, 9);
                        }
gm_lb34:;
                    }
gm_lb33:;
                    uint64_t t250 = gm_l34;
                    uint64_t r251; bool o252 = __builtin_add_overflow(t250, ((uint64_t)1ULL), &r251);
                    uint64_t t253 = r251;
                    gm_l34 = t253;
                }
            }
gm_lb31:;
            uint8_t t254 = (uint8_t)((((uint8_t *)&F.l30))[56] != 0);
            if (t254) {
                memmove(((uint8_t *)&F.l42), ((uint8_t *)&F.l30), 56);
            } else {
                {
                    gm_b16_8 t255 = gm_l0;
                    gm_b16_8 t256 = gm_l4;
                    gm_b16_8 t257 = gm_f_describe_mp0u2mq((void *)NULL, cx, NULL, t256);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t258;
                    GM_ST(gm_b16_8, ((uint8_t *)&t258 + 0), t255);
                    GM_ST(gm_b16_8, ((uint8_t *)&t258 + 16), t257);
                    gm_f_format_eagbsnc((void *)&gm_l60, (void *)NULL, cx, NULL, &t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                if (!t259) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_srcpsceljs9unl1, 212, 21);
                {
                    gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                    gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                    memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                    ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t261);
                    goto gm_fdone;
                }
            }
            {
                gm_b16_8 t262 = gm_l32;
                gm_b16_8 t263 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l42)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t264;
                GM_ST(uint8_t, (uint8_t *)&t264 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t264 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t264 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t264), t263);
                ((uint8_t *)&t264)[16] = 1;
                gm_b16_8 t265 = gm_l31;
                gm_tnpifv20irnph t266;
                GM_ST(gm_b16_8, ((uint8_t *)&t266 + 0), t265);
                gm_f_format_7sll6e8((void *)&gm_l61, (void *)NULL, cx, NULL, t266);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t267 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                if (!t267) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srcpsceljs9unl1, 214, 82);
                gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                gm_t8qqrlhb610a2 t270;
                GM_ST(gm_b16_8, ((uint8_t *)&t270 + 0), t262);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t270 + 16), t264);
                GM_ST(gm_b16_8, ((uint8_t *)&t270 + 40), t269);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
                GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t270);
                goto gm_fdone;
            }
        }
    }
    {
        void * t271 = gm_l2;
        uint8_t t272 = (uint8_t)(((((uint8_t *)t271) + 80))[16] != 0);
        if (t272) {
            gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)t271) + 80));
            gm_l43 = t273;
        } else {
            {
                gm_b16_8 t274 = gm_l0;
                void * t275 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t276)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t276, gm_srcpsceljs9unl1, 217, 87);
                gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)t275) + (uintptr_t)((uint64_t)0ULL) * 96));
                gm_t6j6smj7qoctu t278;
                GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t274);
                GM_ST(gm_b16_8, ((uint8_t *)&t278 + 16), t277);
                gm_f_format_95e0vi0((void *)&gm_l62, (void *)NULL, cx, NULL, &t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t279 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
            if (!t279) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_srcpsceljs9unl1, 217, 17);
            {
                gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
                memset(&(*(gm_tmfqp43enl3pa *)gm_out), 0, sizeof (*(gm_tmfqp43enl3pa *)gm_out));
                ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t281);
                goto gm_fdone;
            }
        }
        gm_b16_8 t282 = gm_l43;
        gm_th2lqbo77pvsc t283;
        memset(&t283, 0, sizeof t283);
        gm_b16_8 t284;
        GM_ST(void *, (uint8_t *)&t284, (void *)(gm_ses9qtp6kdd22));
        GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(7));
        gm_t8qqrlhb610a2 t285;
        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 0), t282);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t285 + 16), t283);
        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 40), t284);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out))[0] = 0;
        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&(*(gm_tmfqp43enl3pa *)gm_out) + 8), t285);
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
inline __attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_read_manifest_u9uajvn F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* text */
    gm_tf1bjg81u0glv gm_l3; /* e */
    GM_STACK_CHECK(&F, 1315u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3ujhu6qph7de gm_l4;
    gm_tm43kqe3e8b4a gm_l5;
    gm_tjgmtdrk88t7m gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t4;
    {
        gm_b16_8 t5 = gm_l1;
        gm_f_read_text_g0qbj1t((void *)&gm_l4, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t6) {
    } else {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        memset(&(*(gm_tikairc198460 *)gm_out), 0, sizeof (*(gm_tikairc198460 *)gm_out));
        ((uint8_t *)&(*(gm_tikairc198460 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 8), t7);
        goto gm_fdone;
    }
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_l2 = t8;
    {
        gm_b16_8 t9 = gm_l2;
        gm_f_parse_manifest_b61s3ln((void *)&gm_l6, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t10) {
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l6) + 8), 320);
    } else {
        {
            memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l6) + 8), 24);
        }
        {
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_tq3m5qgvkpje8 t14;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t11);
            GM_ST(uint64_t, ((uint8_t *)&t14 + 16), t12);
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 24), t13);
            gm_f_format_n6eprd4((void *)&gm_l7, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcpsceljs9unl1, 226, 58);
        {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            memset(&(*(gm_tikairc198460 *)gm_out), 0, sizeof (*(gm_tikairc198460 *)gm_out));
            ((uint8_t *)&(*(gm_tikairc198460 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 8), t17);
            goto gm_fdone;
        }
    }
    {
        gm_tm43kqe3e8b4a t18 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&gm_l5));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tikairc198460 *)gm_out))[0] = 0;
        GM_ST(gm_tm43kqe3e8b4a, ((uint8_t *)&(*(gm_tikairc198460 *)gm_out) + 8), t18);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_record_dep_84vd3la(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_record_dep_84vd3la F;
    void * gm_l0; /* r */
    void * gm_l4; /* keys */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    uint8_t t4 = (uint8_t)((((uint8_t *)a1))[56] != 0);
    if (t4) {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a1), 56);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            {
                void * t5 = gm_l0;
                void * t6 = (void *)(((uint8_t *)t5) + 512);
                void * t7 = gm_f_slot_5e9u9id(t6, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l3)));
                gm_l6 = t7;
            }
            uint8_t t8 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l6));
            gm_l4 = t9;
            {
                void * t10 = gm_l4;
                gm_tftb1odc739t5 t11 = gm_f_push_d61dqrj(t10, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
                gm_l7 = t11;
            }
            uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcpsceljs9unl1, 716, 9);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t13;
        gm_f_list_qeqmcmu((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t13;
    }
    {
        void * t14 = (void *)((uint8_t *)&F.l5);
        gm_tftb1odc739t5 t15 = gm_f_push_d61dqrj(t14, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
        gm_l8 = t15;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcpsceljs9unl1, 720, 5);
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 512);
        gm_b0_1 t19 = gm_f_put_jng5s3d((void *)NULL, cx, NULL, t18, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l3)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) void gm_f_recorded_version_lb5b4vt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_recorded_version_lb5b4vt F;
    gm_b16_8 gm_l0; /* all */
    gm_b16_8 gm_l1; /* reference */
    gm_b16_8 gm_l2; /* reqs */
    uint8_t gm_l3; /* pinned */
    uint8_t gm_l4; /* versioned */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* tag */
    GM_STACK_CHECK(&F, 374u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_tom1ad5bcqgi1 gm_l12;
    gm_b16_8 t11 = gm_l0;
    gm_b16_8 t12 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t12;
    gm_l3 = ((uint8_t)0);
    gm_l4 = ((uint8_t)0);
    gm_b16_8 t13 = gm_l2;
    gm_l5 = t13;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l6;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t19 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t17) + (uintptr_t)t19 * 96), 96);
                        {
                            {
                                uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 48) + 0));
                                uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                gm_l4 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 48) + 0));
                                uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                                if (!t23) {
                                    goto gm_lb8;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                gm_l3 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 95, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 94, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l6;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l6 = t27;
        }
    }
gm_lb1:;
    uint8_t t28 = gm_l3;
    if (t28) {
        gm_l10 = ((uint8_t)1);
    } else {
        uint8_t t29 = gm_l4;
        uint8_t t30 = (uint8_t)!t29;
        gm_l10 = t30;
    }
    uint8_t t31 = gm_l10;
    if (t31) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t32 = gm_l1;
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sn2t7jm5mf8pm));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(4));
        gm_f_strip_prefix_b4949s8((void *)&gm_l11, t32, t33);
    }
    uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
    if (t34) {
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l8 = t35;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t36 = gm_l8;
        gm_f_tag_version_aloeltn((void *)&gm_l12, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l12))[56] != 0);
    if (t37) {
        memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l12), 56);
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t38 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_refs_of_jfglrbh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_refs_of_jfglrbh F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 192));
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 120), 120);
                        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            gm_tmf08dt0mom0q t18 = GM_LD(gm_tmf08dt0mom0q, (((uint8_t *)&F.l4) + 16));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcmp1j2cnqi8i *)gm_out) + 105, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcmp1j2cnqi8i *)gm_out) + 106, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcmp1j2cnqi8i *)gm_out) + 108, 0);
                            GM_ST(gm_tmf08dt0mom0q, ((uint8_t *)&(*(gm_tcmp1j2cnqi8i *)gm_out)), t18);
                            ((uint8_t *)&(*(gm_tcmp1j2cnqi8i *)gm_out))[104] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 465, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
        }
    }
gm_lb1:;
    memset(&(*(gm_tcmp1j2cnqi8i *)gm_out), 0, sizeof (*(gm_tcmp1j2cnqi8i *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcmp1j2cnqi8i gm_f_refs_of_jfglrbh_c(void * a0, gm_b16_8 a1) { gm_tcmp1j2cnqi8i gm_r; gm_f_refs_of_jfglrbh(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq_c(gm_t2f6hp2vik01f a0, gm_t2f6hp2vik01f a1) { return gm_f_req_same_h2pl2bq(&a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_resolver_ms661bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, const void * a3) {
    struct gm_fr_resolver_ms661bf F;
    gm_b16_8 gm_l0; /* root */
    gm_tqs5p4n2tq19v gm_l1; /* opts */
    gm_tl572d7579hns gm_l2; /* refresh */
    GM_STACK_CHECK(&F, 724u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_tqs5p4n2tq19v t3 = gm_l1;
    gm_b16_8 t4 = gm_l0;
    gm_tl572d7579hns t5 = GM_LD(gm_tl572d7579hns, ((uint8_t *)a2));
    gm_tnpifv20irnph t6 = gm_f_store_for_package_347124r((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tv37d3dpt2opu t7 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)a3));
    gm_tin97tt4ud0s1 t8;
    gm_f_key_map_ihrh7ij((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t9;
    gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t10;
    gm_f_list_jsujn2k((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t11;
    gm_f_list_agmtpnt((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t12;
    gm_f_list_epvducg((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t13;
    gm_f_key_map_136uk7q((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14;
    gm_f_list_qeqmcmu((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t15;
    gm_f_key_map_kjnah27((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t16;
    gm_f_key_map_kjnah27((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t17;
    gm_f_key_map_kjnah27((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t18;
    gm_f_key_map_e8nmg3h((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t19;
    gm_f_list_dc4p80b((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t20;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t20 + 0), t19);
    gm_trqfvmjtt4sij t21;
    gm_f_list_nccb2fo((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 20, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 553, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 554, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 556, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 0), t2);
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 16), t3);
    GM_ST(gm_tl572d7579hns, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 24), t5);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 48), t6);
    GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 64), t7);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 112), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 152), t9);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 192), t10);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 232), t11);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 272), t12);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 312), t13);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 352), t14);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 392), t15);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 432), t16);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 472), t17);
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 512), t18);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 552), ((uint8_t)0));
    GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 560), t20);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 600), t21);
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
__attribute__((aligned(4))) void gm_f_run_resolver_tf2ua9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_run_resolver_tf2ua9j F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l3; /* name */
    GM_STACK_CHECK(&F, 1540u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tikairc198460 gm_l7;
    gm_b16_8 gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_tl572d7579hns gm_l10;
    gm_tl572d7579hns gm_l11;
    gm_t62efos6ssciq gm_l12;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
        gm_f_canonicalize_epo7cln((void *)&gm_l5, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_l1 = t10;
    } else {
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_f_format_bpl33sh((void *)&gm_l6, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcpsceljs9unl1, 350, 47);
        {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            memset(&(*(gm_t8a9rhgj2oa25 *)gm_out), 0, sizeof (*(gm_t8a9rhgj2oa25 *)gm_out));
            ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t16);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t17 = gm_l1;
        gm_f_read_manifest_u9uajvn((void *)&gm_l7, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t18) {
    } else {
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        memset(&(*(gm_t8a9rhgj2oa25 *)gm_out), 0, sizeof (*(gm_t8a9rhgj2oa25 *)gm_out));
        ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t19);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l7) + 8), 320);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t20 = gm_l0;
                void * t21 = (void *)(((uint8_t *)t20) + 312);
                gm_f_clear_pirklev(t21);
                void * t22 = gm_l0;
                void * t23 = (void *)(((uint8_t *)t22) + 352);
                gm_f_clear_s0loit2(t23);
                void * t24 = gm_l0;
                void * t25 = (void *)(((uint8_t *)t24) + 392);
                gm_f_clear_qd87n65(t25);
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 472);
                gm_f_clear_qd87n65(t27);
                void * t28 = gm_l0;
                void * t29 = (void *)(((uint8_t *)t28) + 512);
                gm_f_clear_m4blb4l(t29);
                {
                    void * t30 = gm_l0;
                    gm_trqfvmjtt4sij t31;
                    gm_f_list_dc4p80b((void *)&t31, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tin97tt4ud0s1 t32;
                    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 0), t31);
                    GM_ST(gm_tin97tt4ud0s1, (((uint8_t *)t30) + 560), t32);
                }
                void * t33 = gm_l0;
                void * t34 = (void *)(((uint8_t *)t33) + 600);
                gm_f_clear_rm1b3rc(t34);
                void * t35 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t35) + 552), ((uint8_t)0));
                uint8_t t36 = (uint8_t)((((uint8_t *)&F.l2))[16] != 0);
                if (t36) {
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                    gm_l3 = t37;
                } else {
                    gm_b16_8 t38 = gm_l1;
                    gm_f_file_name_em7v8lv((void *)&gm_l9, (void *)NULL, cx, NULL, t38);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
                    if (t39) {
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_l8 = t40;
                    } else {
                        gm_b16_8 t41;
                        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sr0hr1ep2j2vv));
                        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
                        gm_l8 = t41;
                    }
                    gm_b16_8 t42 = gm_l8;
                    gm_l3 = t42;
                }
                {
                    void * t43 = gm_l0;
                    gm_b16_8 t44 = gm_l1;
                    void * t45 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t46 = gm_l3;
                    gm_tqa5rsrlv432m t47;
                    memset(&t47, 0, sizeof t47);
                    gm_f_walk_59naf6k((void *)&gm_l10, (void *)NULL, cx, NULL, t43, t44, t45, t46, &t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (t48) {
                } else {
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                    memset(&(*(gm_t8a9rhgj2oa25 *)gm_out), 0, sizeof (*(gm_t8a9rhgj2oa25 *)gm_out));
                    ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t49);
                    goto gm_fdone;
                }
                void * t50 = gm_l0;
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 552));
                uint8_t t52 = (uint8_t)!t51;
                if (t52) {
                    goto gm_lb1;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t53 = gm_l0;
            uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t53) + 312) + 8));
            uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
            if (!t55) {
                goto gm_lb5;
            }
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t56) + 352) + 8));
            uint8_t t58 = ((t57 == ((uint64_t)0ULL)));
            if (!t58) {
                goto gm_lb5;
            }
            {
                gm_tuu655st500b0 t59;
                memset(&t59, 0, sizeof t59);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 0;
                GM_ST(gm_tuu655st500b0, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t59);
                goto gm_fdone;
            }
        }
gm_lb5:;
    }
gm_lb4:;
    {
        void * t60 = gm_l0;
        gm_f_check_refresh_names_9ih3ok9((void *)&gm_l11, (void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t61) {
    } else {
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        memset(&(*(gm_t8a9rhgj2oa25 *)gm_out), 0, sizeof (*(gm_t8a9rhgj2oa25 *)gm_out));
        ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t62);
        goto gm_fdone;
    }
    {
        void * t63 = gm_l0;
        gm_f_build_lock_hmfdkj8((void *)&gm_l12, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t64) {
    } else {
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        memset(&(*(gm_t8a9rhgj2oa25 *)gm_out), 0, sizeof (*(gm_t8a9rhgj2oa25 *)gm_out));
        ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t65);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l12) + 8), 40);
    }
    {
        void * t66 = gm_l0;
        gm_tin97tt4ud0s1 t67 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)t66) + 560));
        gm_tin97tt4ud0s1 t68 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)&F.l4));
        gm_t08dlk5fmn1l9 t69;
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t69 + 0), t67);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t69 + 40), t68);
        gm_tuu655st500b0 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 81, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 82, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 84, 0);
        GM_ST(gm_t08dlk5fmn1l9, ((uint8_t *)&t70), t69);
        ((uint8_t *)&t70)[80] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out))[0] = 0;
        GM_ST(gm_tuu655st500b0, ((uint8_t *)&(*(gm_t8a9rhgj2oa25 *)gm_out) + 8), t70);
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
inline __attribute__((aligned(4))) uint8_t gm_f_satisfies_mlodg56(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_satisfies_mlodg56 F;
    gm_b16_8 gm_l0; /* reference */
    gm_b16_8 gm_l1; /* rev */
    gm_b16_8 gm_l3; /* t */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* r */
    gm_b16_8 gm_l7; /* tag */
    GM_STACK_CHECK(&F, 599u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_tom1ad5bcqgi1 gm_l13;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_l0;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_ses9qtp6kdd22));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
            uint8_t t12 = (gm_eq0iqiubfm7e(((uint8_t *)&t10), ((uint8_t *)&t11)));
            gm_l9 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l3 = t15;
            gm_b16_8 t16 = gm_l0;
            gm_b16_8 t17 = gm_l3;
            gm_tnpifv20irnph t18;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
            gm_f_format_7sll6e8((void *)&gm_l10, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcpsceljs9unl1, 111, 33);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t21)));
            gm_l9 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l4 = t25;
            gm_b16_8 t26 = gm_l0;
            gm_b16_8 t27 = gm_l4;
            gm_tnpifv20irnph t28;
            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
            gm_f_format_asjuqmr((void *)&gm_l11, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcpsceljs9unl1, 112, 36);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t31)));
            gm_l9 = t32;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)4ULL));
            if (!t34) {
                goto gm_lb5;
            }
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l5 = t35;
            gm_b16_8 t36 = gm_l1;
            gm_b16_8 t37 = gm_l5;
            uint8_t t38 = gm_f_starts_with_6vvt01e(t36, t37);
            gm_l9 = t38;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
            if (!t40) {
                goto gm_lb6;
            }
            {
                memmove(((uint8_t *)&F.l6), (((uint8_t *)a2) + 8), 40);
            }
            {
                gm_b16_8 t41 = gm_l0;
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sn2t7jm5mf8pm));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
                gm_f_strip_prefix_b4949s8((void *)&gm_l12, t41, t42);
            }
            uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
            if (t43) {
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                gm_l7 = t44;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                gm_b16_8 t45 = gm_l7;
                gm_f_tag_version_aloeltn((void *)&gm_l13, (void *)NULL, cx, NULL, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l13))[56] != 0);
            if (t46) {
                memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l13), 56);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                void * t47 = (void *)((uint8_t *)&F.l6);
                uint8_t t48 = gm_f_req_matches_dflcrek(t47, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l8)));
                F.ret = t48;
                goto gm_fret;
            }
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 109, 12);
    }
gm_lb1:;
    uint8_t t49 = gm_l9;
    F.ret = t49;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha_mismatch_lq7bp8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sha_mismatch_lq7bp8e F;
    gm_b16_8 gm_l0; /* dep */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* found */
    gm_b16_8 gm_l3; /* expected */
    gm_b16_8 gm_l4; /* origin */
    GM_STACK_CHECK(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l5;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_b16_8 t5 = gm_l4;
        gm_b16_8 t6 = gm_l3;
        gm_tud7oovfvnf43 t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t2);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t3);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 32), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 48), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 64), t6);
        gm_f_format_ia6bs6h((void *)&gm_l5, (void *)NULL, cx, NULL, &t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcpsceljs9unl1, 588, 12);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_subdir_of_gjqk3qb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_subdir_of_gjqk3qb F;
    gm_b16_8 gm_l0; /* co */
    gm_th2lqbo77pvsc gm_l1; /* sub */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* p */
    GM_STACK_CHECK(&F, 244u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l4;
    uint8_t t2 = (uint8_t)((((uint8_t *)a1))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l2 = t3;
    } else {
        gm_b16_8 t4 = gm_l0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t4);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6 = gm_l2;
        gm_b16_8 t7 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_canonicalize_epo7cln((void *)&gm_l4, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l3 = t9;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t10 = gm_l3;
    gm_b16_8 t11 = gm_l0;
    uint8_t t12 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)!t12;
    if (t13) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t14 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t14);
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
inline __attribute__((aligned(4))) void gm_f_summarize_archive_cicsplu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_summarize_archive_cicsplu F;
    gm_b16_8 gm_l0; /* dep */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* hash */
    gm_b16_8 gm_l3; /* dir */
    gm_th2lqbo77pvsc gm_l4; /* locked */
    gm_b16_8 gm_l5; /* checksum */
    gm_b16_8 gm_l6; /* l */
    gm_b16_8 gm_l7; /* canon */
    GM_STACK_CHECK(&F, 718u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_t3ujhu6qph7de gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        gm_b16_8 t4 = gm_l3;
        gm_f_tree_checksum_k6o2imm((void *)&gm_l8, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t5) {
    } else {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
        ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t6);
        goto gm_fdone;
    }
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_l5 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l6 = t9;
            gm_b16_8 t10 = gm_l6;
            gm_b16_8 t11 = gm_l5;
            uint8_t t12 = (!gm_eq0iqiubfm7e(((uint8_t *)&t10), ((uint8_t *)&t11)));
            if (t12) {
                {
                    gm_b16_8 t13 = gm_l0;
                    gm_b16_8 t14 = gm_l6;
                    gm_b16_8 t15 = gm_l5;
                    gm_b16_8 t16 = gm_l3;
                    gm_t0uuhaljsf410 t17;
                    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t13);
                    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t14);
                    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 32), t15);
                    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 48), t16);
                    gm_f_format_dvcii4b((void *)&gm_l9, (void *)NULL, cx, NULL, &t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcpsceljs9unl1, 596, 21);
                {
                    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                    memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
                    ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t20);
                    goto gm_fdone;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_b16_8 t21 = gm_l3;
        gm_f_canonicalize_epo7cln((void *)&gm_l10, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t22) {
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l7 = t23;
    } else {
        {
            gm_b16_8 t24 = gm_l3;
            gm_tnpifv20irnph t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
            gm_f_format_bpl33sh((void *)&gm_l11, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcpsceljs9unl1, 605, 45);
        {
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            memset(&(*(gm_tq6bq450l0idg *)gm_out), 0, sizeof (*(gm_tq6bq450l0idg *)gm_out));
            ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t28);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t29 = gm_l1;
        gm_b16_8 t30 = gm_l2;
        gm_b16_8 t31 = gm_l7;
        gm_b16_8 t32 = gm_l5;
        gm_t0uuhaljsf410 t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t29);
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 16), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 32), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 48), t32);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out))[0] = 0;
        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&(*(gm_tq6bq450l0idg *)gm_out) + 8), t33);
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
inline __attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_tag_version_aloeltn F;
    gm_b16_8 gm_l0; /* tag */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l2;
    gm_tom1ad5bcqgi1 gm_l3;
    gm_tvtl3n8qta358 gm_l4;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssdi9g220if8h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
        gm_f_strip_prefix_b4949s8((void *)&gm_l2, t2, t3);
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l1 = t5;
    } else {
        gm_b16_8 t6 = gm_l0;
        gm_l1 = t6;
    }
    {
        gm_b16_8 t7 = gm_l1;
        gm_f_parse_version_9a6ns50((void *)&gm_l4, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t8) {
        gm_tu1089hhijadf t9 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&gm_l4) + 8));
        GM_ST(uint8_t, (uint8_t *)&gm_l3 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l3 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l3 + 60, 0);
        GM_ST(gm_tu1089hhijadf, ((uint8_t *)&gm_l3), t9);
        ((uint8_t *)&gm_l3)[56] = 1;
    } else {
        memset(&gm_l3, 0, sizeof gm_l3);
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 64);
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
__attribute__((aligned(4))) void gm_f_walk_59naf6k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_walk_59naf6k F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* dir */
    void * gm_l2; /* manifest */
    gm_b16_8 gm_l3; /* display */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint8_t gm_l9; /* any_fetched */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l21; /* p */
    gm_b16_8 gm_l22; /* joined */
    gm_th2lqbo77pvsc gm_l23; /* canon */
    gm_b16_8 gm_l25; /* c */
    gm_b16_8 gm_l26; /* target */
    gm_b16_8 gm_l31; /* co */
    gm_b16_8 gm_l32; /* pkg_dir */
    gm_b16_8 gm_l34; /* n */
    gm_b16_8 gm_l38; /* co */
    gm_b16_8 gm_l39; /* pkg_dir */
    uint8_t gm_l41; /* dup */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    GM_STACK_CHECK(&F, 6232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_toa5kh9gm9738 gm_l45;
    uint8_t gm_l46;
    uint8_t gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tabb710d9mq2e gm_l51;
    uint8_t gm_l52;
    gm_tqetrgo0q24b5 gm_l53;
    uint8_t gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tftb1odc739t5 gm_l58;
    uint8_t gm_l59;
    gm_tikairc198460 gm_l60;
    gm_tl572d7579hns gm_l61;
    gm_tmfqp43enl3pa gm_l62;
    gm_t3ujhu6qph7de gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_b16_8 gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tikairc198460 gm_l69;
    gm_tl572d7579hns gm_l70;
    gm_tq6bq450l0idg gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_th2lqbo77pvsc gm_l73;
    gm_b16_8 gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tikairc198460 gm_l78;
    gm_b16_8 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tftb1odc739t5 gm_l81;
    gm_tl572d7579hns gm_l82;
    gm_tftb1odc739t5 gm_l83;
    {
        void * t51 = gm_l0;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 600));
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        gm_l5 = t53;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t54 = gm_l6;
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t56 = (t54 < t55);
                if (!t56) {
                    goto gm_lb1;
                }
                {
                    {
                        uint64_t t57 = gm_l6;
                        void * t58 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t60 = gm_l6;
                        gm_tk9n3vlila9qg t61 = GM_LD(gm_tk9n3vlila9qg, (((uint8_t *)t58) + (uintptr_t)t60 * 40));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l45 + 0), t57);
                        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)&gm_l45 + 8), t61);
                    }
                    {
                        {
                            uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&gm_l45));
                            gm_l7 = t62;
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l45) + 8), 40);
                            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                            gm_b16_8 t64 = gm_l1;
                            uint8_t t65 = (gm_eq0iqiubfm7e(((uint8_t *)&t63), ((uint8_t *)&t64)));
                            if (t65) {
                                gm_l9 = ((uint8_t)0);
                                void * t66 = gm_l0;
                                void * t67 = GM_LD(void *, (((uint8_t *)t66) + 600));
                                uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t66) + 600) + 8));
                                uint64_t t69 = gm_l7;
                                uint64_t t70 = (uintptr_t)t69;
                                uint64_t t71 = (uintptr_t)t68;
                                if (t70 > t71 || t71 > (uintptr_t)t68) gm_trap_slice(t70, t71, t68, gm_srcpsceljs9unl1, 730, 22);
                                uint64_t t72 = ((uintptr_t)t68) - (uintptr_t)t69;
                                gm_b16_8 t73;
                                GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t67 + (uintptr_t)t69 * 40)));
                                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72));
                                gm_l10 = t73;
                                gm_l11 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t74 = gm_l11;
                                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint8_t t76 = (t74 < t75);
                                        if (!t76) {
                                            goto gm_lb6;
                                        }
                                        {
                                            void * t77 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint64_t t79 = gm_l11;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l12), (((uint8_t *)t77) + (uintptr_t)t79 * 40), 40);
                                                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 32));
                                                    if (t80) {
                                                        gm_l9 = ((uint8_t)1);
                                                    }
                                                    goto gm_lb9;
                                                }
gm_lb10:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 730, 13);
                                            }
gm_lb9:;
                                        }
gm_lb8:;
                                        uint64_t t81 = gm_l11;
                                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                                        uint64_t t84 = r82;
                                        gm_l11 = t84;
                                    }
                                }
gm_lb6:;
                                {
                                    {
                                        uint8_t t85 = (uint8_t)((((uint8_t *)&F.l4))[56] != 0);
                                        if (t85) {
                                            goto gm_lb12;
                                        }
                                        gm_l47 = ((uint8_t)0);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    {
                                        gm_l47 = ((uint8_t)1);
                                        goto gm_lb11;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 733, 16);
                                }
gm_lb11:;
                                uint8_t t86 = gm_l47;
                                if (t86) {
                                    gm_l46 = ((uint8_t)1);
                                } else {
                                    uint8_t t87 = gm_l9;
                                    gm_l46 = t87;
                                }
                                uint8_t t88 = gm_l46;
                                if (t88) {
                                    {
                                        gm_trqfvmjtt4sij t89;
                                        gm_f_list_epvducg((void *)&t89, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l13 = t89;
                                    }
                                    void * t90 = gm_l0;
                                    void * t91 = GM_LD(void *, (((uint8_t *)t90) + 600));
                                    uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)t90) + 600) + 8));
                                    uint64_t t93 = gm_l7;
                                    uint64_t t94 = (uintptr_t)t93;
                                    uint64_t t95 = (uintptr_t)t92;
                                    if (t94 > t95 || t95 > (uintptr_t)t92) gm_trap_slice(t94, t95, t92, gm_srcpsceljs9unl1, 735, 26);
                                    uint64_t t96 = ((uintptr_t)t92) - (uintptr_t)t93;
                                    gm_b16_8 t97;
                                    GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t91 + (uintptr_t)t93 * 40)));
                                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(t96));
                                    gm_l14 = t97;
                                    gm_l15 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t98 = gm_l15;
                                            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                            uint8_t t100 = (t98 < t99);
                                            if (!t100) {
                                                goto gm_lb14;
                                            }
                                            {
                                                void * t101 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                uint64_t t103 = gm_l15;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l16), (((uint8_t *)t101) + (uintptr_t)t103 * 40), 40);
                                                        void * t104 = (void *)((uint8_t *)&F.l13);
                                                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 16));
                                                        gm_tftb1odc739t5 t106 = gm_f_push_3ef746b(t104, t105);
                                                        gm_l48 = t106;
                                                        uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                        if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcpsceljs9unl1, 735, 47);
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 735, 17);
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                            uint64_t t108 = gm_l15;
                                            uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                            uint64_t t111 = r109;
                                            gm_l15 = t111;
                                        }
                                    }
gm_lb14:;
                                    void * t112 = (void *)((uint8_t *)&F.l13);
                                    gm_b16_8 t113 = gm_l3;
                                    gm_tftb1odc739t5 t114 = gm_f_push_3ef746b(t112, t113);
                                    gm_l49 = t114;
                                    uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                    if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcpsceljs9unl1, 736, 17);
                                    {
                                        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                                        gm_b16_8 t118;
                                        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sdbvaa960ee0f));
                                        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(4));
                                        gm_f_join_sfknljb((void *)&gm_l50, (void *)NULL, cx, NULL, t117, t118);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                    if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcpsceljs9unl1, 737, 60);
                                    {
                                        gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                                        gm_tnpifv20irnph t121;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t120);
                                        gm_f_format_fnh5q7b((void *)&gm_l51, (void *)NULL, cx, NULL, t121);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                    if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srcpsceljs9unl1, 737, 25);
                                    {
                                        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                                        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t124);
                                        goto gm_fdone;
                                    }
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 727, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t125 = gm_l6;
                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                uint64_t t128 = r126;
                gm_l6 = t128;
            }
        }
gm_lb1:;
    }
    {
        void * t129 = gm_l0;
        void * t130 = (void *)(((uint8_t *)t129) + 560);
        gm_b16_8 t131 = gm_l1;
        gm_f_deps_of_i4no6j9((void *)&gm_l53, t130, t131);
        {
            {
                uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l53))[40] != 0);
                if (t132) {
                    goto gm_lb20;
                }
                gm_l52 = ((uint8_t)0);
                goto gm_lb19;
            }
gm_lb20:;
            {
                gm_l52 = ((uint8_t)1);
                goto gm_lb19;
            }
gm_lb21:;
            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 742, 8);
        }
gm_lb19:;
        uint8_t t133 = gm_l52;
        if (t133) {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
            goto gm_fdone;
        }
        void * t134 = gm_l0;
        void * t135 = (void *)(((uint8_t *)t134) + 600);
        gm_b16_8 t136 = gm_l1;
        gm_b16_8 t137 = gm_l3;
        {
            {
                uint8_t t138 = (uint8_t)((((uint8_t *)&F.l4))[56] != 0);
                if (t138) {
                    goto gm_lb23;
                }
                gm_l54 = ((uint8_t)0);
                goto gm_lb22;
            }
gm_lb23:;
            {
                gm_l54 = ((uint8_t)1);
                goto gm_lb22;
            }
gm_lb24:;
            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 743, 62);
        }
gm_lb22:;
        uint8_t t139 = gm_l54;
        gm_tk9n3vlila9qg t140;
        GM_ST(uint8_t, (uint8_t *)&t140 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t140 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t140 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t136);
        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 16), t137);
        GM_ST(uint8_t, ((uint8_t *)&t140 + 32), t139);
        gm_tftb1odc739t5 t141 = gm_f_push_fp6nnvl(t135, &t140);
        gm_l55 = t141;
        uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
        if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcpsceljs9unl1, 743, 5);
        gm_trqfvmjtt4sij t143;
        gm_f_list_0pjrgaq((void *)&t143, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t143;
        void * t144 = gm_l2;
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)t144) + 48));
        gm_l18 = t145;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t146 = gm_l19;
                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t148 = (t146 < t147);
                    if (!t148) {
                        goto gm_lb25;
                    }
                    {
                        {
                            void * t149 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            uint64_t t151 = gm_l19;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t149) + (uintptr_t)t151 * 120), 120);
                                            {
                                                {
                                                    {
                                                        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                                                        uint8_t t153 = (uint8_t)(t152 == ((uint8_t)0ULL));
                                                        if (!t153) {
                                                            goto gm_lb31;
                                                        }
                                                        gm_b16_8 t154 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l20) + 24) + 8));
                                                        gm_l21 = t154;
                                                        gm_b16_8 t155 = gm_l1;
                                                        gm_b16_8 t156 = gm_l21;
                                                        gm_b16_8 t157 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t155, t156);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_b16_8 t158 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t157);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l22 = t158;
                                                        {
                                                            gm_b16_8 t159 = gm_l22;
                                                            gm_f_canonicalize_epo7cln((void *)&gm_l23, (void *)NULL, cx, NULL, t159);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        {
                                                            {
                                                                uint8_t t160 = (uint8_t)((((uint8_t *)&F.l4))[56] != 0);
                                                                if (!t160) {
                                                                    goto gm_lb33;
                                                                }
                                                                memmove(((uint8_t *)&F.l24), ((uint8_t *)&F.l4), 56);
                                                                {
                                                                    {
                                                                        uint8_t t161 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                                                                        if (!t161) {
                                                                            goto gm_lb35;
                                                                        }
                                                                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                                                        gm_l25 = t162;
                                                                        gm_b16_8 t163 = gm_l25;
                                                                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 40));
                                                                        uint8_t t165 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t163, t164);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t166 = (uint8_t)!t165;
                                                                        if (t166) {
                                                                            {
                                                                                gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                                gm_b16_8 t168 = gm_l3;
                                                                                gm_b16_8 t169 = gm_l21;
                                                                                gm_tfeab279c16rr t170;
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t167);
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t170 + 16), t168);
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t170 + 32), t169);
                                                                                gm_f_format_0c1hioc((void *)&gm_l56, (void *)NULL, cx, NULL, &t170);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                            }
                                                                            uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                                            if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_srcpsceljs9unl1, 753, 37);
                                                                            {
                                                                                gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                                                                                gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t173);
                                                                                goto gm_fdone;
                                                                            }
                                                                        }
                                                                        goto gm_lb34;
                                                                    }
gm_lb35:;
                                                                    {
                                                                        gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                        gm_b16_8 t175 = gm_l3;
                                                                        gm_b16_8 t176 = gm_l21;
                                                                        gm_tfeab279c16rr t177;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t177 + 0), t174);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t177 + 16), t175);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t177 + 32), t176);
                                                                        gm_f_format_o83d7mn((void *)&gm_l57, (void *)NULL, cx, NULL, &t177);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                                    if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srcpsceljs9unl1, 756, 33);
                                                                    {
                                                                        gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                                                        gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t180);
                                                                        goto gm_fdone;
                                                                    }
                                                                }
gm_lb34:;
                                                                goto gm_lb32;
                                                            }
gm_lb33:;
                                                        }
gm_lb32:;
                                                        uint8_t t181 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                                                        if (t181) {
                                                            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                                            gm_l26 = t182;
                                                        } else {
                                                            gm_b16_8 t183 = gm_l22;
                                                            gm_l26 = t183;
                                                        }
                                                        {
                                                            void * t184 = (void *)((uint8_t *)&F.l17);
                                                            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t186 = gm_l26;
                                                            gm_t6j6smj7qoctu t187;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t185);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t187 + 16), t186);
                                                            gm_tftb1odc739t5 t188 = gm_f_push_i535kr1(t184, &t187);
                                                            gm_l58 = t188;
                                                        }
                                                        uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                        if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcpsceljs9unl1, 760, 17);
                                                        {
                                                            {
                                                                {
                                                                    {
                                                                        uint8_t t190 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                                                                        if (t190) {
                                                                            goto gm_lb39;
                                                                        }
                                                                        gm_l59 = ((uint8_t)0);
                                                                        goto gm_lb38;
                                                                    }
gm_lb39:;
                                                                    {
                                                                        gm_l59 = ((uint8_t)1);
                                                                        goto gm_lb38;
                                                                    }
gm_lb40:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 761, 20);
                                                                }
gm_lb38:;
                                                                uint8_t t191 = gm_l59;
                                                                if (!t191) {
                                                                    goto gm_lb37;
                                                                }
                                                                gm_b16_8 t192 = gm_l26;
                                                                gm_b16_8 t193;
                                                                GM_ST(void *, (uint8_t *)&t193, (void *)(gm_se24c7hpp401n));
                                                                GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(10));
                                                                gm_b16_8 t194 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t192, t193);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t195 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t194);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                if (!t195) {
                                                                    goto gm_lb37;
                                                                }
                                                                {
                                                                    gm_b16_8 t196 = gm_l26;
                                                                    gm_f_read_manifest_u9uajvn((void *)&gm_l60, (void *)NULL, cx, NULL, t196);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                                if (t197) {
                                                                } else {
                                                                    gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t198);
                                                                    goto gm_fdone;
                                                                }
                                                                {
                                                                    memmove(((uint8_t *)&F.l27), (((uint8_t *)&gm_l60) + 8), 320);
                                                                }
                                                                {
                                                                    void * t199 = gm_l0;
                                                                    gm_b16_8 t200 = gm_l26;
                                                                    void * t201 = (void *)((uint8_t *)&F.l27);
                                                                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                    gm_tqa5rsrlv432m t203 = GM_LD(gm_tqa5rsrlv432m, ((uint8_t *)&F.l4));
                                                                    gm_f_walk_59naf6k((void *)&gm_l61, (void *)NULL, cx, NULL, t199, t200, t201, t202, &t203);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                                                if (t204) {
                                                                } else {
                                                                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t205);
                                                                    goto gm_fdone;
                                                                }
                                                                goto gm_lb36;
                                                            }
gm_lb37:;
                                                        }
gm_lb36:;
                                                        goto gm_lb30;
                                                    }
gm_lb31:;
                                                }
                                                {
                                                    {
                                                        uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                                                        uint8_t t207 = (uint8_t)(t206 == ((uint8_t)1ULL));
                                                        if (!t207) {
                                                            goto gm_lb41;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l28), ((((uint8_t *)&F.l20) + 24) + 8), 88);
                                                        }
                                                        {
                                                            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                                            gm_b16_8 t209 = gm_f_normalize_url_faghdja(t208);
                                                            gm_th2lqbo77pvsc t210 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l28) + 64));
                                                            GM_ST(gm_b16_8, ((uint8_t *)&F.l29 + 0), t209);
                                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l29 + 16), t210);
                                                        }
                                                        {
                                                            void * t211 = gm_l0;
                                                            gm_b16_8 t212 = gm_l3;
                                                            gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                                            gm_toq2f2q4opoa3 t215 = GM_LD(gm_toq2f2q4opoa3, (((uint8_t *)&F.l28) + 16));
                                                            gm_t2f6hp2vik01f t216;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t216 + 0), t212);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t216 + 16), t213);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t216 + 32), t214);
                                                            GM_ST(gm_toq2f2q4opoa3, ((uint8_t *)&t216 + 48), t215);
                                                            gm_b0_1 t217 = gm_f_add_req_klu87ju((void *)NULL, cx, NULL, t211, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)), &t216);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        {
                                                            void * t218 = gm_l0;
                                                            gm_t6ok5tvcjc200 t219 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l29));
                                                            gm_f_choose_9ee155u((void *)&gm_l62, (void *)NULL, cx, NULL, t218, &t219);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                        if (t220) {
                                                        } else {
                                                            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t221);
                                                            goto gm_fdone;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l30), (((uint8_t *)&gm_l62) + 8), 56);
                                                        }
                                                        {
                                                            void * t222 = gm_l0;
                                                            gm_t6ok5tvcjc200 t223 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l29));
                                                            gm_t8qqrlhb610a2 t224 = GM_LD(gm_t8qqrlhb610a2, ((uint8_t *)&F.l30));
                                                            gm_f_ensure_checkout_hvmsso1((void *)&gm_l63, (void *)NULL, cx, NULL, t222, &t223, &t224);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                                        if (t225) {
                                                        } else {
                                                            gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t226);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                                                        gm_l31 = t227;
                                                        {
                                                            gm_b16_8 t228 = gm_l31;
                                                            gm_th2lqbo77pvsc t229 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l28) + 64));
                                                            gm_f_subdir_of_gjqk3qb((void *)&gm_l64, (void *)NULL, cx, NULL, t228, &t229);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t230 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                                                        if (t230) {
                                                            gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l64));
                                                            gm_l32 = t231;
                                                        } else {
                                                            {
                                                                gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                                                uint8_t t234 = (uint8_t)(((((uint8_t *)&F.l28) + 64))[16] != 0);
                                                                if (t234) {
                                                                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 64));
                                                                    gm_l65 = t235;
                                                                } else {
                                                                    gm_b16_8 t236;
                                                                    GM_ST(void *, (uint8_t *)&t236, (void *)(gm_sr4qbrk6tlgaa));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(0));
                                                                    gm_l65 = t236;
                                                                }
                                                                gm_b16_8 t237 = gm_l65;
                                                                gm_tfeab279c16rr t238;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t238 + 0), t232);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t238 + 16), t233);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t238 + 32), t237);
                                                                gm_f_format_lbar0n6((void *)&gm_l66, (void *)NULL, cx, NULL, &t238);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                            if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_srcpsceljs9unl1, 772, 29);
                                                            {
                                                                gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                                                                gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t241);
                                                                goto gm_fdone;
                                                            }
                                                        }
                                                        gm_b16_8 t242 = gm_l32;
                                                        gm_b16_8 t243;
                                                        GM_ST(void *, (uint8_t *)&t243, (void *)(gm_se24c7hpp401n));
                                                        GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(10));
                                                        gm_b16_8 t244 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t242, t243);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t245 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t244);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t246 = (uint8_t)!t245;
                                                        if (t246) {
                                                            {
                                                                gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                                                gm_t6j6smj7qoctu t249;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t249 + 0), t247);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t249 + 16), t248);
                                                                gm_f_format_ro2n9te((void *)&gm_l67, (void *)NULL, cx, NULL, &t249);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                            if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srcpsceljs9unl1, 775, 29);
                                                            {
                                                                gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                                                gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t252);
                                                                goto gm_fdone;
                                                            }
                                                        }
                                                        {
                                                            void * t253 = gm_l0;
                                                            gm_b0_1 t254 = gm_f_record_dep_84vd3la((void *)NULL, cx, NULL, t253, &(*(gm_tqa5rsrlv432m *)((uint8_t *)&F.l4)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        {
                                                            void * t255 = (void *)((uint8_t *)&F.l17);
                                                            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t257 = gm_l32;
                                                            gm_t6j6smj7qoctu t258;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t258 + 0), t256);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t258 + 16), t257);
                                                            gm_tftb1odc739t5 t259 = gm_f_push_i535kr1(t255, &t258);
                                                            gm_l68 = t259;
                                                        }
                                                        uint8_t t260 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                                        if (!t260) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcpsceljs9unl1, 778, 17);
                                                        {
                                                            gm_b16_8 t261 = gm_l32;
                                                            gm_f_read_manifest_u9uajvn((void *)&gm_l69, (void *)NULL, cx, NULL, t261);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                                        if (t262) {
                                                        } else {
                                                            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t263);
                                                            goto gm_fdone;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l33), (((uint8_t *)&gm_l69) + 8), 320);
                                                        }
                                                        {
                                                            {
                                                                uint8_t t264 = (uint8_t)((((uint8_t *)&F.l33))[16] != 0);
                                                                if (!t264) {
                                                                    goto gm_lb43;
                                                                }
                                                                gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
                                                                gm_l34 = t265;
                                                                void * t266 = gm_l0;
                                                                void * t267 = (void *)(((uint8_t *)t266) + 472);
                                                                gm_b16_8 t268 = gm_l34;
                                                                gm_b0_1 t269 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t267, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)), t268);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                goto gm_lb42;
                                                            }
gm_lb43:;
                                                        }
gm_lb42:;
                                                        {
                                                            void * t270 = gm_l0;
                                                            gm_b16_8 t271 = gm_l32;
                                                            void * t272 = (void *)((uint8_t *)&F.l33);
                                                            gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t274 = gm_l31;
                                                            gm_t6ok5tvcjc200 t275 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l29));
                                                            gm_tbqru6ibafrpr t276;
                                                            GM_ST(gm_t6ok5tvcjc200, ((uint8_t *)&t276 + 0), t275);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t276 + 40), t274);
                                                            gm_tqa5rsrlv432m t277;
                                                            GM_ST(uint8_t, (uint8_t *)&t277 + 57, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t277 + 58, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t277 + 60, 0);
                                                            GM_ST(gm_tbqru6ibafrpr, ((uint8_t *)&t277), t276);
                                                            ((uint8_t *)&t277)[56] = 1;
                                                            gm_f_walk_59naf6k((void *)&gm_l70, (void *)NULL, cx, NULL, t270, t271, t272, t273, &t277);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t278 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                                        if (t278) {
                                                        } else {
                                                            gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t279);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb30;
                                                    }
gm_lb41:;
                                                }
                                                {
                                                    {
                                                        uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                                                        uint8_t t281 = (uint8_t)(t280 == ((uint8_t)2ULL));
                                                        if (!t281) {
                                                            goto gm_lb44;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l35), ((((uint8_t *)&F.l20) + 24) + 8), 64);
                                                        }
                                                        {
                                                            void * t282 = gm_l0;
                                                            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_tt008mgut8glb t284 = GM_LD(gm_tt008mgut8glb, ((uint8_t *)&F.l35));
                                                            gm_f_ensure_archive_70kh53q((void *)&gm_l71, (void *)NULL, cx, NULL, t282, t283, &t284);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                                        if (t285) {
                                                        } else {
                                                            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t286);
                                                            goto gm_fdone;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l36), (((uint8_t *)&gm_l71) + 8), 64);
                                                        }
                                                        {
                                                            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                                            gm_tnpifv20irnph t288;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t288 + 0), t287);
                                                            gm_f_format_s20j3pl((void *)&gm_l72, (void *)NULL, cx, NULL, t288);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                        if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srcpsceljs9unl1, 785, 33);
                                                        {
                                                            gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                                                            gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&t290));
                                                            gm_th2lqbo77pvsc t292 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l35) + 40));
                                                            GM_ST(gm_b16_8, ((uint8_t *)&F.l37 + 0), t291);
                                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l37 + 16), t292);
                                                        }
                                                        gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 32));
                                                        gm_l38 = t293;
                                                        {
                                                            gm_b16_8 t294 = gm_l38;
                                                            gm_th2lqbo77pvsc t295 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l35) + 40));
                                                            gm_f_subdir_of_gjqk3qb((void *)&gm_l73, (void *)NULL, cx, NULL, t294, &t295);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t296 = (uint8_t)((((uint8_t *)&gm_l73))[16] != 0);
                                                        if (t296) {
                                                            gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l73));
                                                            gm_l39 = t297;
                                                        } else {
                                                            {
                                                                gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                                                uint8_t t300 = (uint8_t)(((((uint8_t *)&F.l35) + 40))[16] != 0);
                                                                if (t300) {
                                                                    gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 40));
                                                                    gm_l74 = t301;
                                                                } else {
                                                                    gm_b16_8 t302;
                                                                    GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sr4qbrk6tlgaa));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(0));
                                                                    gm_l74 = t302;
                                                                }
                                                                gm_b16_8 t303 = gm_l74;
                                                                gm_tfeab279c16rr t304;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t304 + 0), t298);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t304 + 16), t299);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t304 + 32), t303);
                                                                gm_f_format_lbar0n6((void *)&gm_l75, (void *)NULL, cx, NULL, &t304);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            uint8_t t305 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                                            if (!t305) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_srcpsceljs9unl1, 788, 29);
                                                            {
                                                                gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                                                                gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t307);
                                                                goto gm_fdone;
                                                            }
                                                        }
                                                        gm_b16_8 t308 = gm_l39;
                                                        gm_b16_8 t309;
                                                        GM_ST(void *, (uint8_t *)&t309, (void *)(gm_se24c7hpp401n));
                                                        GM_ST(uintptr_t, (uint8_t *)&t309 + 8, (uintptr_t)(10));
                                                        gm_b16_8 t310 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t308, t309);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t311 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t310);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t312 = (uint8_t)!t311;
                                                        if (t312) {
                                                            {
                                                                gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                                                gm_t6j6smj7qoctu t315;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t315 + 0), t313);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t315 + 16), t314);
                                                                gm_f_format_ro2n9te((void *)&gm_l76, (void *)NULL, cx, NULL, &t315);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            uint8_t t316 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                                            if (!t316) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srcpsceljs9unl1, 791, 29);
                                                            {
                                                                gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                                                                gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)&t317));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t318);
                                                                goto gm_fdone;
                                                            }
                                                        }
                                                        {
                                                            void * t319 = gm_l0;
                                                            gm_b0_1 t320 = gm_f_record_dep_84vd3la((void *)NULL, cx, NULL, t319, &(*(gm_tqa5rsrlv432m *)((uint8_t *)&F.l4)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        {
                                                            void * t321 = (void *)((uint8_t *)&F.l17);
                                                            gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t323 = gm_l39;
                                                            gm_t6j6smj7qoctu t324;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t322);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t324 + 16), t323);
                                                            gm_tftb1odc739t5 t325 = gm_f_push_i535kr1(t321, &t324);
                                                            gm_l77 = t325;
                                                        }
                                                        uint8_t t326 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                                        if (!t326) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srcpsceljs9unl1, 794, 17);
                                                        {
                                                            gm_b16_8 t327 = gm_l39;
                                                            gm_f_read_manifest_u9uajvn((void *)&gm_l78, (void *)NULL, cx, NULL, t327);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t328 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                        if (t328) {
                                                        } else {
                                                            gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t329);
                                                            goto gm_fdone;
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&F.l40), (((uint8_t *)&gm_l78) + 8), 320);
                                                        }
                                                        {
                                                            void * t330 = gm_l0;
                                                            void * t331 = (void *)(((uint8_t *)t330) + 472);
                                                            uint8_t t332 = (uint8_t)((((uint8_t *)&F.l40))[16] != 0);
                                                            gm_t6ok5tvcjc200 t333 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l37));
                                                            if (t332) {
                                                                gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
                                                                gm_l79 = t334;
                                                            } else {
                                                                gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                                gm_l79 = t335;
                                                            }
                                                            gm_b16_8 t336 = gm_l79;
                                                            gm_b0_1 t337 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t331, &t333, t336);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        void * t338 = gm_l0;
                                                        void * t339 = (void *)(((uint8_t *)t338) + 272);
                                                        gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                        gm_tftb1odc739t5 t341 = gm_f_push_3ef746b(t339, t340);
                                                        gm_l80 = t341;
                                                        uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                        if (!t342) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcpsceljs9unl1, 797, 17);
                                                        gm_l41 = ((uint8_t)0);
                                                        void * t343 = gm_l0;
                                                        gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)t343) + 352));
                                                        gm_l42 = t344;
                                                        gm_l43 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t345 = gm_l43;
                                                                uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                                uint8_t t347 = (t345 < t346);
                                                                if (!t347) {
                                                                    goto gm_lb45;
                                                                }
                                                                {
                                                                    void * t348 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                                                    uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                                    uint64_t t350 = gm_l43;
                                                                    {
                                                                        {
                                                                            {
                                                                                memmove(((uint8_t *)&F.l44), (((uint8_t *)t348) + (uintptr_t)t350 * 40), 40);
                                                                            }
                                                                            {
                                                                                uint8_t t351 = gm_f_key_equal_n71njnf(&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l44)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                                                                                if (t351) {
                                                                                    gm_l41 = ((uint8_t)1);
                                                                                }
                                                                            }
                                                                            goto gm_lb48;
                                                                        }
gm_lb49:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 799, 17);
                                                                    }
gm_lb48:;
                                                                }
gm_lb47:;
                                                                uint64_t t352 = gm_l43;
                                                                uint64_t r353; bool o354 = __builtin_add_overflow(t352, ((uint64_t)1ULL), &r353);
                                                                uint64_t t355 = r353;
                                                                gm_l43 = t355;
                                                            }
                                                        }
gm_lb45:;
                                                        uint8_t t356 = gm_l41;
                                                        uint8_t t357 = (uint8_t)!t356;
                                                        if (t357) {
                                                            void * t358 = gm_l0;
                                                            void * t359 = (void *)(((uint8_t *)t358) + 352);
                                                            gm_tftb1odc739t5 t360 = gm_f_push_d61dqrj(t359, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                                                            gm_l81 = t360;
                                                            uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                                            if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srcpsceljs9unl1, 802, 30);
                                                        }
                                                        {
                                                            void * t362 = gm_l0;
                                                            gm_b16_8 t363 = gm_l39;
                                                            void * t364 = (void *)((uint8_t *)&F.l40);
                                                            gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                                            gm_b16_8 t366 = gm_l38;
                                                            gm_t6ok5tvcjc200 t367 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&F.l37));
                                                            gm_tbqru6ibafrpr t368;
                                                            GM_ST(gm_t6ok5tvcjc200, ((uint8_t *)&t368 + 0), t367);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t368 + 40), t366);
                                                            gm_tqa5rsrlv432m t369;
                                                            GM_ST(uint8_t, (uint8_t *)&t369 + 57, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t369 + 58, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t369 + 60, 0);
                                                            GM_ST(gm_tbqru6ibafrpr, ((uint8_t *)&t369), t368);
                                                            ((uint8_t *)&t369)[56] = 1;
                                                            gm_f_walk_59naf6k((void *)&gm_l82, (void *)NULL, cx, NULL, t362, t363, t364, t365, &t369);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t370 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                                                        if (t370) {
                                                        } else {
                                                            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t371);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb30;
                                                    }
gm_lb44:;
                                                }
                                                {
                                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 746, 9);
                                                }
                                            }
gm_lb30:;
                                        }
                                        {
                                            goto gm_lb28;
                                        }
                                    }
gm_lb29:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 745, 5);
                                }
                            }
gm_lb28:;
                        }
                    }
gm_lb27:;
                }
                {
                    uint64_t t372 = gm_l19;
                    uint64_t r373; bool o374 = __builtin_add_overflow(t372, ((uint64_t)1ULL), &r373);
                    uint64_t t375 = r373;
                    gm_l19 = t375;
                }
            }
        }
gm_lb25:;
    }
    {
        void * t376 = gm_l0;
        void * t377 = (void *)(((uint8_t *)t376) + 600);
        gm_ta2tdjm589013 t378;
        gm_f_pop_rnfkbb9((void *)&t378, t377);
        void * t379 = gm_l0;
        void * t380 = (void *)(((uint8_t *)t379) + 560);
        gm_b16_8 t381 = gm_l1;
        gm_trqfvmjtt4sij t382 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l17));
        gm_tl7hpf5nlilqq t383;
        GM_ST(gm_b16_8, ((uint8_t *)&t383 + 0), t381);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t383 + 16), t382);
        gm_tftb1odc739t5 t384 = gm_f_push_7mbittu(t380, &t383);
        gm_l83 = t384;
        uint8_t t385 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
        if (!t385) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srcpsceljs9unl1, 808, 5);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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

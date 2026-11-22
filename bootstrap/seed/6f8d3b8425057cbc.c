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
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; int64_t v1; gm_b16_8 v2; } f1; } gm_t5e047ltlbe02;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t5e047ltlbe02 f0; uint8_t f1; uint8_t pe[7]; } gm_t1m5mugfio2m4;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e047ltlbe02) == 24 && _Alignof(gm_t5e047ltlbe02) == 8 && offsetof(gm_t5e047ltlbe02, f0) == 0 && offsetof(gm_t5e047ltlbe02, f1) == 8, "layout of `Const`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1m5mugfio2m4) == 32 && _Alignof(gm_t1m5mugfio2m4) == 8 && offsetof(gm_t1m5mugfio2m4, f0) == 0 && offsetof(gm_t1m5mugfio2m4, f1) == 24, "layout of `?Const`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
struct gm_fr_compare_hq04o5p {
    gm_frame h;
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_eval_5c9oefa {
    gm_frame h;
    gm_tv2uf4leil5hq l16; /* b */
    gm_tet9guhev5r4u l17; /* i */
    gm_tc3oht4ksth9d l19; /* arg */
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_field_abqqbar {
    gm_frame h;
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_type_size_h6snnvm {
    gm_frame h;
    gm_t913bb735pvt5 ret;
};
struct gm_fr_when_cond_9a46a5h {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqsqt22rph28(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt4fm78ifa7(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_compare_hq04o5p(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_compare_hq04o5p_c(gm_ts6oqppjh0ba3 a0, gm_t5e047ltlbe02 a1, gm_t5e047ltlbe02 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_eval_5c9oefa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_target_field_abqqbar(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_target_field_abqqbar_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_type_size_h6snnvm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
extern const uint8_t gm_s030jgonh2s3s[105];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s3ngn7me40qa4[5];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6iroad3kr6qd[7];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_sa42h1oh66nds[9];
extern const uint8_t gm_saean4ms0g52k[8];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sjmmigkb19htg[4];
extern const uint8_t gm_sla1l2pt1mfvt[3];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smbmfla2idsdo[68];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_soqlsukui24da[7];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src2pgaip05o19o[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
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
inline bool gm_eqsqt22rph28(const uint8_t *a, const uint8_t *b) {
    return GM_LD(int64_t, a) == GM_LD(int64_t, b);
}
inline bool gm_eqt4fm78ifa7(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq929lennfd3(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eqsqt22rph28(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq0iqiubfm7e(a + 8, b + 8);
    return true;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t6 = r4;
    uint32_t t7 = (uint32_t)t6;
    F.ret = t7;
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        memset(&(*(gm_t9p38b040grms *)gm_out), 0, sizeof (*(gm_t9p38b040grms *)gm_out));
        goto gm_fdone;
    }
    {
        gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &t3);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9p38b040grms gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_to_u128_haka99r((void *)&gm_l2, &t2);
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        unsigned __int128 t4 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l2));
        gm_l1 = t4;
    } else {
        gm_tf5srh4mp4b3i t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    unsigned __int128 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7;
    uint64_t t8 = (uint64_t)t6;
    gm_tf5srh4mp4b3i t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[8] = 1;
    gm_tf5srh4mp4b3i t10;
    memset(&t10, 0, sizeof t10);
    t7 = ((unsigned __int128)(t6) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t9 : t10;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_compare_hq04o5p(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compare_hq04o5p F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t5e047ltlbe02 gm_l1; /* a */
    gm_t5e047ltlbe02 gm_l2; /* b */
    int64_t gm_l3; /* x */
    int64_t gm_l4; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t1m5mugfio2m4 gm_l5;
    {
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)12ULL));
            if (!t17) {
                goto gm_lb2;
            }
            {
                gm_t5e047ltlbe02 t18 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a1));
                gm_t5e047ltlbe02 t19 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a2));
                uint8_t t20 = (gm_eqt4fm78ifa7(((uint8_t *)&t18), ((uint8_t *)&t19)));
                gm_t5e047ltlbe02 t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t21 + 8), t20);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t21);
                ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)13ULL));
            if (!t23) {
                goto gm_lb3;
            }
            {
                gm_t5e047ltlbe02 t24 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a1));
                gm_t5e047ltlbe02 t25 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a2));
                uint8_t t26 = (!gm_eqt4fm78ifa7(((uint8_t *)&t24), ((uint8_t *)&t25)));
                gm_t5e047ltlbe02 t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t27 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t27 + 8), t26);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t27);
                ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 83, 5);
    }
gm_lb1:;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb6;
            }
            int64_t t30 = GM_LD(int64_t, (((uint8_t *)a1) + 8));
            gm_l3 = t30;
            {
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
                    if (!t32) {
                        goto gm_lb8;
                    }
                    int64_t t33 = GM_LD(int64_t, (((uint8_t *)a2) + 8));
                    gm_l4 = t33;
                    {
                        {
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)16ULL));
                            if (!t35) {
                                goto gm_lb10;
                            }
                            {
                                int64_t t36 = gm_l3;
                                int64_t t37 = gm_l4;
                                uint8_t t38 = (t36 < t37);
                                gm_t5e047ltlbe02 t39;
                                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t39 + 8), t38);
                                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l5), t39);
                                ((uint8_t *)&gm_l5)[24] = 1;
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)17ULL));
                            if (!t41) {
                                goto gm_lb11;
                            }
                            {
                                int64_t t42 = gm_l3;
                                int64_t t43 = gm_l4;
                                uint8_t t44 = (t42 <= t43);
                                gm_t5e047ltlbe02 t45;
                                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t45 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t45 + 8), t44);
                                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l5), t45);
                                ((uint8_t *)&gm_l5)[24] = 1;
                            }
                            goto gm_lb9;
                        }
gm_lb11:;
                        {
                            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)18ULL));
                            if (!t47) {
                                goto gm_lb12;
                            }
                            {
                                int64_t t48 = gm_l3;
                                int64_t t49 = gm_l4;
                                uint8_t t50 = (t48 > t49);
                                gm_t5e047ltlbe02 t51;
                                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t51 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t51 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t51 + 8), t50);
                                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l5), t51);
                                ((uint8_t *)&gm_l5)[24] = 1;
                            }
                            goto gm_lb9;
                        }
gm_lb12:;
                        {
                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)19ULL));
                            if (!t53) {
                                goto gm_lb13;
                            }
                            {
                                int64_t t54 = gm_l3;
                                int64_t t55 = gm_l4;
                                uint8_t t56 = (t54 >= t55);
                                gm_t5e047ltlbe02 t57;
                                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t57 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t57 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t57 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t57 + 8), t56);
                                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l5), t57);
                                ((uint8_t *)&gm_l5)[24] = 1;
                            }
                            goto gm_lb9;
                        }
gm_lb13:;
                        {
                            memset(&gm_l5, 0, sizeof gm_l5);
                            goto gm_lb9;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 90, 12);
                    }
gm_lb9:;
                    memmove(gm_out, ((uint8_t *)&gm_l5), 32);
                    goto gm_fdone;
                }
gm_lb8:;
                memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                goto gm_fdone;
            }
gm_lb7:;
        }
gm_lb6:;
        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
        goto gm_fdone;
    }
gm_lb5:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_compare_hq04o5p_c(gm_ts6oqppjh0ba3 a0, gm_t5e047ltlbe02 a1, gm_t5e047ltlbe02 a2) { gm_t1m5mugfio2m4 gm_r; gm_f_compare_hq04o5p(&gm_r, a0, &a1, &a2); return gm_r; }
__attribute__((aligned(4))) void gm_f_eval_5c9oefa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_eval_5c9oefa F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    void * gm_l5; /* lit */
    uint64_t gm_l6; /* v */
    void * gm_l7; /* x */
    void * gm_l8; /* x */
    uint8_t gm_l9; /* b */
    gm_t08olq97rscn3 gm_l10; /* id */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    gm_t08olq97rscn3 gm_l13; /* m */
    gm_t08olq97rscn3 gm_l14; /* id */
    uint64_t gm_l15; /* d */
    gm_b16_8 gm_l18; /* args */
    gm_t08olq97rscn3 gm_l20; /* t */
    int64_t gm_l21; /* bytes */
    gm_ts6oqppjh0ba3 gm_l22; /* op */
    void * gm_l23; /* lhs */
    void * gm_l24; /* rhs */
    gm_t5e047ltlbe02 gm_l25; /* a */
    uint8_t gm_l26; /* x */
    uint8_t gm_l27; /* y */
    gm_t5e047ltlbe02 gm_l28; /* b */
    GM_STACK_CHECK(&F, 1486u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l29;
    uint32_t gm_l30;
    gm_t9805qtonbsag gm_l31;
    gm_t1m5mugfio2m4 gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    gm_tf5srh4mp4b3i gm_l35;
    gm_tf5srh4mp4b3i gm_l36;
    uint8_t gm_l37;
    uint8_t gm_l38;
    gm_t913bb735pvt5 gm_l39;
    gm_t1m5mugfio2m4 gm_l40;
    gm_t1m5mugfio2m4 gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_t1m5mugfio2m4 gm_l45;
    {
        uint32_t t33 = gm_l3;
        uint8_t t34 = (t33 > ((uint32_t)64ULL));
        if (t34) {
            memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
            goto gm_fdone;
        }
        void * t35 = gm_l2;
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)3ULL));
                    if (!t37) {
                        goto gm_lb2;
                    }
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 8));
                    gm_l4 = t38;
                    {
                        uint8_t t39 = gm_l4;
                        gm_t5e047ltlbe02 t40;
                        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t40 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t40 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t40 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t40 + 8), t39);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t40);
                        ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb2:;
                {
                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)0ULL));
                    if (!t42) {
                        goto gm_lb3;
                    }
                    void * t43 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
                    gm_l5 = t43;
                    {
                        void * t44 = gm_l5;
                        gm_tf5srh4mp4b3i t45 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)t44)));
                        gm_l29 = t45;
                    }
                    uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l29))[8] != 0);
                    if (t46) {
                        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l29));
                        gm_l6 = t47;
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    uint64_t t48 = gm_l6;
                    gm_t9805qtonbsag t49;
                    uint32_t t50 = (uint32_t)t48;
                    gm_t9805qtonbsag t51;
                    GM_ST(uint8_t, (uint8_t *)&t51 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t51 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t51), t50);
                    ((uint8_t *)&t51)[4] = 1;
                    gm_t9805qtonbsag t52;
                    memset(&t52, 0, sizeof t52);
                    t49 = ((unsigned __int128)(t48) <= (((unsigned __int128)0ULL << 64) | 4294967295ULL)) ? t51 : t52;
                    gm_l31 = t49;
                    uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l31))[4] != 0);
                    if (t53) {
                        uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)&gm_l31));
                        gm_l30 = t54;
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        uint32_t t55 = gm_l30;
                        int64_t t56 = (int64_t)t55;
                        gm_t5e047ltlbe02 t57;
                        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)1ULL));
                        GM_ST(int64_t, ((uint8_t *)&t57 + 8), t56);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t57);
                        ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb3:;
                {
                    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)9ULL));
                    if (!t59) {
                        goto gm_lb4;
                    }
                    void * t60 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
                    gm_l7 = t60;
                    void * t61 = gm_l0;
                    uint64_t t62 = gm_l1;
                    void * t63 = gm_l7;
                    uint32_t t64 = gm_l3;
                    gm_f_eval_5c9oefa((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), (void *)NULL, cx, NULL, t61, t62, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb4:;
                {
                    uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)12ULL));
                    if (!t66) {
                        goto gm_lb5;
                    }
                    uint8_t t67 = GM_LD(uint8_t, (((((uint8_t *)t35) + 8) + 8) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)5ULL));
                    if (!t68) {
                        goto gm_lb5;
                    }
                    void * t69 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 8));
                    gm_l8 = t69;
                    {
                        {
                            {
                                void * t70 = gm_l0;
                                uint64_t t71 = gm_l1;
                                void * t72 = gm_l8;
                                uint32_t t73 = gm_l3;
                                gm_f_eval_5c9oefa((void *)&gm_l32, (void *)NULL, cx, NULL, t70, t71, t72, t73);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l32))[24] != 0);
                            if (!t74) {
                                goto gm_lb7;
                            }
                            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)0ULL));
                            if (!t76) {
                                goto gm_lb7;
                            }
                            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 8));
                            gm_l9 = t77;
                            {
                                uint8_t t78 = gm_l9;
                                uint8_t t79 = (uint8_t)!t78;
                                gm_t5e047ltlbe02 t80;
                                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t80 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t80 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t80 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t80 + 8), t79);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t80);
                                ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                                goto gm_fdone;
                            }
                        }
gm_lb7:;
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
gm_lb6:;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)8ULL));
                    if (!t82) {
                        goto gm_lb8;
                    }
                    {
                        memmove(((uint8_t *)&gm_l10), ((((uint8_t *)t35) + 8) + 8), 32);
                    }
                    {
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                        gm_t5e047ltlbe02 t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t84);
                        ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb8:;
                {
                    uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)22ULL));
                    if (!t86) {
                        goto gm_lb9;
                    }
                    void * t87 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
                    gm_l11 = t87;
                    memmove(((uint8_t *)&gm_l12), (((((uint8_t *)t35) + 8) + 8) + 8), 32);
                    {
                        {
                            {
                                void * t88 = gm_l11;
                                uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
                                uint8_t t90 = (uint8_t)(t89 == ((uint8_t)6ULL));
                                if (!t90) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&gm_l13), ((((uint8_t *)t88) + 8) + 8), 32);
                            }
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                            gm_b16_8 t92;
                            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_snoai0j6d9u36));
                            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(6));
                            uint8_t t93 = (!gm_eq0iqiubfm7e(((uint8_t *)&t91), ((uint8_t *)&t92)));
                            if (t93) {
                                gm_l33 = ((uint8_t)1);
                            } else {
                                void * t94 = gm_l0;
                                uint64_t t95 = gm_l1;
                                gm_b16_8 t96;
                                GM_ST(void *, (uint8_t *)&t96, (void *)(gm_snoai0j6d9u36));
                                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(6));
                                gm_tf5srh4mp4b3i t97 = gm_f_lookup_59juqoo(t94, t95, t96);
                                gm_l35 = t97;
                                {
                                    {
                                        uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l35))[8] != 0);
                                        if (t98) {
                                            goto gm_lb13;
                                        }
                                        gm_l34 = ((uint8_t)0);
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                    {
                                        gm_l34 = ((uint8_t)1);
                                        goto gm_lb12;
                                    }
gm_lb14:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 47, 42);
                                }
gm_lb12:;
                                uint8_t t99 = gm_l34;
                                gm_l33 = t99;
                            }
                            uint8_t t100 = gm_l33;
                            if (t100) {
                                memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                                goto gm_fdone;
                            }
                            {
                                void * t101 = gm_l0;
                                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                                gm_f_target_field_abqqbar((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), t101, t102);
                                goto gm_fdone;
                            }
                        }
gm_lb11:;
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
gm_lb10:;
                }
gm_lb9:;
                {
                    uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)6ULL));
                    if (!t104) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&gm_l14), ((((uint8_t *)t35) + 8) + 8), 32);
                    void * t105 = gm_l0;
                    uint64_t t106 = gm_l1;
                    gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                    gm_tf5srh4mp4b3i t108 = gm_f_lookup_59juqoo(t105, t106, t107);
                    gm_l36 = t108;
                    uint8_t t109 = (uint8_t)((((uint8_t *)&gm_l36))[8] != 0);
                    if (t109) {
                        uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                        gm_l15 = t110;
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        {
                            {
                                void * t111 = gm_l0;
                                void * t112 = GM_LD(void *, (((uint8_t *)t111) + 240));
                                uint64_t t113 = GM_LD(uint64_t, ((((uint8_t *)t111) + 240) + 8));
                                uint64_t t114 = gm_l15;
                                if ((uintptr_t)t114 >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)t114, t113, gm_src2pgaip05o19o, 53, 28);
                                uint8_t t115 = GM_LD(uint8_t, ((((((uint8_t *)t112) + (uintptr_t)t114 * 216) + 40) + 64) + 0));
                                uint8_t t116 = (uint8_t)(t115 == ((uint8_t)0ULL));
                                if (!t116) {
                                    goto gm_lb17;
                                }
                                memmove(((uint8_t *)&F.l16), ((((((uint8_t *)t112) + (uintptr_t)t114 * 216) + 40) + 64) + 8), 64);
                            }
                            gm_t9qkh0rsooc8d t117 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l16) + 32));
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t117) + 0));
                            uint8_t t119 = (uint8_t)((t118 == ((uint8_t)1ULL)));
                            gm_t9qkh0rsooc8d t120;
                            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)1ULL));
                            if (t119) {
                                gm_l37 = ((uint8_t)1);
                            } else {
                                {
                                    {
                                        uint8_t t121 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l16) + 24)) != 0);
                                        if (t121) {
                                            goto gm_lb19;
                                        }
                                        gm_l38 = ((uint8_t)0);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    {
                                        gm_l38 = ((uint8_t)1);
                                        goto gm_lb18;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 54, 34);
                                }
gm_lb18:;
                                uint8_t t122 = gm_l38;
                                gm_l37 = t122;
                            }
                            uint8_t t123 = gm_l37;
                            if (t123) {
                                memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                                goto gm_fdone;
                            }
                            {
                                void * t124 = gm_l0;
                                uint64_t t125 = gm_l1;
                                void * t126 = GM_LD(void *, (((uint8_t *)&F.l16) + 40));
                                uint32_t t127 = gm_l3;
                                uint32_t r128; bool o129 = __builtin_add_overflow(t127, ((uint32_t)1ULL), &r128);
                                if (o129) gm_trap("integer overflow in `+`", gm_src2pgaip05o19o, 55, 43);
                                uint32_t t130 = r128;
                                gm_f_eval_5c9oefa((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), (void *)NULL, cx, NULL, t124, t125, t126, t130);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                        }
gm_lb17:;
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
gm_lb16:;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t132 = (uint8_t)(t131 == ((uint8_t)49ULL));
                    if (!t132) {
                        goto gm_lb21;
                    }
                    memmove(((uint8_t *)&F.l17), ((((uint8_t *)t35) + 8) + 8), 56);
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                    gm_b16_8 t134;
                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_saean4ms0g52k));
                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(7));
                    uint8_t t135 = (!gm_eq0iqiubfm7e(((uint8_t *)&t133), ((uint8_t *)&t134)));
                    if (t135) {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    uint8_t t136 = (uint8_t)(((((uint8_t *)&F.l17) + 32))[16] != 0);
                    if (t136) {
                        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 32));
                        gm_l18 = t137;
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t139 = (!(t138 == ((uint64_t)1ULL)));
                    if (t139) {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        {
                            void * t140 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t141, gm_src2pgaip05o19o, 61, 33);
                            uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t140) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                            uint8_t t143 = (uint8_t)(t142 == ((uint8_t)0ULL));
                            if (!t143) {
                                goto gm_lb23;
                            }
                            memmove(((uint8_t *)&F.l19), ((((uint8_t *)t140) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                            {
                                {
                                    uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 8) + 0));
                                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)6ULL));
                                    if (!t145) {
                                        goto gm_lb25;
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l20), ((((uint8_t *)&F.l19) + 8) + 8), 32);
                                    }
                                    void * t146 = gm_l0;
                                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                    gm_t913bb735pvt5 t148 = gm_f_type_size_h6snnvm(t146, t147);
                                    gm_l39 = t148;
                                    uint8_t t149 = (uint8_t)((((uint8_t *)&gm_l39))[8] != 0);
                                    if (t149) {
                                        int64_t t150 = GM_LD(int64_t, ((uint8_t *)&gm_l39));
                                        gm_l21 = t150;
                                    } else {
                                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                                        goto gm_fdone;
                                    }
                                    {
                                        int64_t t151 = gm_l21;
                                        gm_t5e047ltlbe02 t152;
                                        GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t152 + 16, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
                                        GM_ST(int64_t, ((uint8_t *)&t152 + 8), t151);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                                        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t152);
                                        ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                                        goto gm_fdone;
                                    }
                                }
gm_lb25:;
                                memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                                goto gm_fdone;
                            }
gm_lb24:;
                        }
gm_lb23:;
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
gm_lb22:;
                }
gm_lb21:;
                {
                    uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)13ULL));
                    if (!t154) {
                        goto gm_lb26;
                    }
                    gm_ts6oqppjh0ba3 t155 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t35) + 8) + 8));
                    gm_l22 = t155;
                    void * t156 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 8));
                    gm_l23 = t156;
                    void * t157 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 16));
                    gm_l24 = t157;
                    {
                        void * t158 = gm_l0;
                        uint64_t t159 = gm_l1;
                        void * t160 = gm_l23;
                        uint32_t t161 = gm_l3;
                        gm_f_eval_5c9oefa((void *)&gm_l40, (void *)NULL, cx, NULL, t158, t159, t160, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t162 = (uint8_t)((((uint8_t *)&gm_l40))[24] != 0);
                    if (t162) {
                        memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l40), 24);
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    gm_ts6oqppjh0ba3 t163 = gm_l22;
                    gm_ts6oqppjh0ba3 t164;
                    GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)20ULL));
                    gm_ts6oqppjh0ba3 t165;
                    GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)21ULL));
                    gm_t9ldr1q5hn9fq t166;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t166 + 0), t164);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t166 + 2), t165);
                    uint8_t t167 = 0;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t166) + 0, ((uint8_t *)&t163))) t167 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t166) + 2, ((uint8_t *)&t163))) t167 = 1;
                    uint8_t t168 = (uint8_t)(t167);
                    if (t168) {
                        {
                            {
                                uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                uint8_t t170 = (uint8_t)(t169 == ((uint8_t)0ULL));
                                if (!t170) {
                                    goto gm_lb28;
                                }
                                uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
                                gm_l26 = t171;
                                {
                                    {
                                        {
                                            void * t172 = gm_l0;
                                            uint64_t t173 = gm_l1;
                                            void * t174 = gm_l24;
                                            uint32_t t175 = gm_l3;
                                            gm_f_eval_5c9oefa((void *)&gm_l41, (void *)NULL, cx, NULL, t172, t173, t174, t175);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        uint8_t t176 = (uint8_t)((((uint8_t *)&gm_l41))[24] != 0);
                                        if (!t176) {
                                            goto gm_lb30;
                                        }
                                        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l41) + 0));
                                        uint8_t t178 = (uint8_t)(t177 == ((uint8_t)0ULL));
                                        if (!t178) {
                                            goto gm_lb30;
                                        }
                                        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l41) + 8));
                                        gm_l27 = t179;
                                        gm_ts6oqppjh0ba3 t180 = gm_l22;
                                        uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
                                        uint8_t t182 = (uint8_t)((t181 == ((uint8_t)20ULL)));
                                        gm_ts6oqppjh0ba3 t183;
                                        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)20ULL));
                                        if (t182) {
                                            uint8_t t184 = gm_l26;
                                            if (t184) {
                                                uint8_t t185 = gm_l27;
                                                gm_l43 = t185;
                                            } else {
                                                gm_l43 = ((uint8_t)0);
                                            }
                                            uint8_t t186 = gm_l43;
                                            gm_l42 = t186;
                                        } else {
                                            uint8_t t187 = gm_l26;
                                            if (t187) {
                                                gm_l44 = ((uint8_t)1);
                                            } else {
                                                uint8_t t188 = gm_l27;
                                                gm_l44 = t188;
                                            }
                                            uint8_t t189 = gm_l44;
                                            gm_l42 = t189;
                                        }
                                        {
                                            uint8_t t190 = gm_l42;
                                            gm_t5e047ltlbe02 t191;
                                            GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                                            GM_ST(uint8_t, (uint8_t *)&t191 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t191 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t191 + 12, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t191 + 16, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)0ULL));
                                            GM_ST(uint8_t, ((uint8_t *)&t191 + 8), t190);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 25, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 26, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out) + 28, 0);
                                            GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out)), t191);
                                            ((uint8_t *)&(*(gm_t1m5mugfio2m4 *)gm_out))[24] = 1;
                                            goto gm_fdone;
                                        }
                                    }
gm_lb30:;
                                    memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                                    goto gm_fdone;
                                }
gm_lb29:;
                            }
gm_lb28:;
                            memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                            goto gm_fdone;
                        }
gm_lb27:;
                    }
                    {
                        void * t192 = gm_l0;
                        uint64_t t193 = gm_l1;
                        void * t194 = gm_l24;
                        uint32_t t195 = gm_l3;
                        gm_f_eval_5c9oefa((void *)&gm_l45, (void *)NULL, cx, NULL, t192, t193, t194, t195);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t196 = (uint8_t)((((uint8_t *)&gm_l45))[24] != 0);
                    if (t196) {
                        memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l45), 24);
                    } else {
                        memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        gm_ts6oqppjh0ba3 t197 = gm_l22;
                        gm_t5e047ltlbe02 t198 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)&gm_l25));
                        gm_t5e047ltlbe02 t199 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)&gm_l28));
                        gm_f_compare_hq04o5p((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), t197, &t198, &t199);
                        goto gm_fdone;
                    }
                }
gm_lb26:;
            }
            {
                {
                    memset(&(*(gm_t1m5mugfio2m4 *)gm_out), 0, sizeof (*(gm_t1m5mugfio2m4 *)gm_out));
                    goto gm_fdone;
                }
gm_lb31:;
                gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 33, 5);
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
inline __attribute__((aligned(4))) void gm_f_target_field_abqqbar(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_target_field_abqqbar F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 514u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t1m5mugfio2m4 gm_l2;
    gm_b16_8 gm_l3;
    {
        {
            gm_b16_8 t9 = gm_l1;
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sla1l2pt1mfvt));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(2));
            uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
            if (!t11) {
                goto gm_lb2;
            }
            {
                void * t12 = gm_l0;
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 88) + 24));
                gm_t5e047ltlbe02 t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
                GM_ST(uint8_t, (uint8_t *)&gm_l2 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l2 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l2 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l2), t14);
                ((uint8_t *)&gm_l2)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t15 = gm_l1;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s3ngn7me40qa4));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
            uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) {
                goto gm_lb3;
            }
            {
                void * t18 = gm_l0;
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 88) + 40));
                gm_t5e047ltlbe02 t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
                GM_ST(uint8_t, (uint8_t *)&gm_l2 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l2 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l2 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l2), t20);
                ((uint8_t *)&gm_l2)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t21 = gm_l1;
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_soqlsukui24da));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(6));
            uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
            if (!t23) {
                goto gm_lb4;
            }
            void * t24 = gm_l0;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 88) + 8));
            if (t25) {
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s6iroad3kr6qd));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
                gm_l3 = t26;
            } else {
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sjmmigkb19htg));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(3));
                gm_l3 = t27;
            }
            {
                gm_b16_8 t28 = gm_l3;
                gm_t5e047ltlbe02 t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
                GM_ST(uint8_t, (uint8_t *)&gm_l2 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l2 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l2 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l2), t29);
                ((uint8_t *)&gm_l2)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t30 = gm_l1;
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sa42h1oh66nds));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(8));
            uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
            if (!t32) {
                goto gm_lb5;
            }
            {
                void * t33 = gm_l0;
                void * t34 = (void *)(((uint8_t *)t33) + 88);
                uint32_t t35 = gm_f_ptr_bits_glps5i7(t34);
                int64_t t36 = (int64_t)t35;
                gm_t5e047ltlbe02 t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                GM_ST(int64_t, ((uint8_t *)&t37 + 8), t36);
                GM_ST(uint8_t, (uint8_t *)&gm_l2 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l2 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l2 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l2), t37);
                ((uint8_t *)&gm_l2)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_b16_8 t38 = gm_l1;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sgdafdv2n5c25));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(4));
            uint8_t t40 = (gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
            if (!t40) {
                goto gm_lb6;
            }
            {
                void * t41 = gm_l0;
                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t41) + 72));
                gm_t5e047ltlbe02 t43;
                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 8), t42);
                GM_ST(uint8_t, (uint8_t *)&gm_l2 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l2 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l2 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&gm_l2), t43);
                ((uint8_t *)&gm_l2)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            memset(&gm_l2, 0, sizeof gm_l2);
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 100, 65);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l2), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_target_field_abqqbar_c(void * a0, gm_b16_8 a1) { gm_t1m5mugfio2m4 gm_r; gm_f_target_field_abqqbar(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_type_size_h6snnvm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_size_h6snnvm F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    int64_t gm_l2; /* bits */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    int64_t gm_l6; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 841u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t913bb735pvt5 gm_l7;
    gm_t913bb735pvt5 gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    {
        gm_b16_8 t10 = gm_l1;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
        gm_t6j6smj7qoctu t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
        uint8_t t14 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t13) + 0, ((uint8_t *)&t10))) t14 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t13) + 16, ((uint8_t *)&t10))) t14 = 1;
        uint8_t t15 = (uint8_t)(t14);
        if (t15) {
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 88));
            int64_t t18 = (int64_t)t17;
            gm_t913bb735pvt5 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[8] = 1;
            F.ret = t19;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_shnig5tqtdedi));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sm9e81djuncnu));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sqqajgod2bb4o));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
        gm_tfeab279c16rr t24;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t22);
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 32), t23);
        uint8_t t25 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 0, ((uint8_t *)&t20))) t25 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 16, ((uint8_t *)&t20))) t25 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 32, ((uint8_t *)&t20))) t25 = 1;
        uint8_t t26 = (uint8_t)(t25);
        if (t26) {
            gm_b16_8 t27 = gm_l1;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
            uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
            if (t29) {
                gm_t913bb735pvt5 t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t30), ((int64_t)2LL));
                ((uint8_t *)&t30)[8] = 1;
                gm_l7 = t30;
            } else {
                gm_b16_8 t31 = gm_l1;
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sm9e81djuncnu));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
                uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
                if (t33) {
                    gm_t913bb735pvt5 t34;
                    GM_ST(uint8_t, (uint8_t *)&t34 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t34 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t34), ((int64_t)4LL));
                    ((uint8_t *)&t34)[8] = 1;
                    gm_l8 = t34;
                } else {
                    gm_t913bb735pvt5 t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t35), ((int64_t)8LL));
                    ((uint8_t *)&t35)[8] = 1;
                    gm_l8 = t35;
                }
                gm_t913bb735pvt5 t36 = gm_l8;
                gm_l7 = t36;
            }
            gm_t913bb735pvt5 t37 = gm_l7;
            F.ret = t37;
            goto gm_fret;
        }
    }
    gm_b16_8 t38 = gm_l1;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s929lennfd3g6));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(4));
    uint8_t t40 = (gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
    if (t40) {
        gm_t913bb735pvt5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t41), ((int64_t)1LL));
        ((uint8_t *)&t41)[8] = 1;
        F.ret = t41;
        goto gm_fret;
    }
    gm_b16_8 t42 = gm_l1;
    uint8_t t43 = gm_f_is_reserved_type_name_tuq4qsj(t42);
    uint8_t t44 = (uint8_t)!t43;
    if (t44) {
        gm_l10 = ((uint8_t)1);
    } else {
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t46 = (t45 < ((uint64_t)2ULL));
        gm_l10 = t46;
    }
    uint8_t t47 = gm_l10;
    if (t47) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t49, gm_src2pgaip05o19o, 114, 59);
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t48) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t51;
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t51 + 1), ((uint8_t)105ULL));
        uint8_t t52 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 0, ((uint8_t *)&t50))) t52 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 1, ((uint8_t *)&t50))) t52 = 1;
        uint8_t t53 = (uint8_t)(!t52);
        gm_l9 = t53;
    }
    uint8_t t54 = gm_l9;
    if (t54) {
        gm_t913bb735pvt5 t55;
        memset(&t55, 0, sizeof t55);
        F.ret = t55;
        goto gm_fret;
    }
    gm_l2 = ((int64_t)0LL);
    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t58 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t59 = (uintptr_t)t57;
    if (t58 > t59 || t59 > (uintptr_t)t57) gm_trap_slice(t58, t59, t57, gm_src2pgaip05o19o, 116, 14);
    uint64_t t60 = ((uintptr_t)t57) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t56 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
    gm_l3 = t61;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t62 = gm_l4;
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t64 = (t62 < t63);
            if (!t64) {
                goto gm_lb1;
            }
            {
                void * t65 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t67 = gm_l4;
                {
                    {
                        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t65) + (uintptr_t)t67 * 1));
                        gm_l5 = t68;
                        int64_t t69 = gm_l2;
                        int64_t r70; bool o71 = __builtin_mul_overflow(t69, ((int64_t)10LL), &r70);
                        if (o71) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 116, 33);
                        int64_t t72 = r70;
                        uint8_t t73 = gm_l5;
                        uint8_t r74; bool o75 = __builtin_sub_overflow(t73, ((uint8_t)48ULL), &r74);
                        if (o75) gm_trap("integer overflow in `-`", gm_src2pgaip05o19o, 116, 49);
                        uint8_t t76 = r74;
                        int64_t t77 = (int64_t)t76;
                        int64_t r78; bool o79 = __builtin_add_overflow(t72, t77, &r78);
                        if (o79) gm_trap("integer overflow in `+`", gm_src2pgaip05o19o, 116, 33);
                        int64_t t80 = r78;
                        gm_l2 = t80;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 116, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t81 = gm_l4;
            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
            uint64_t t84 = r82;
            gm_l4 = t84;
        }
    }
gm_lb1:;
    gm_l6 = ((int64_t)1LL);
    {
        for (;;) {
            int64_t t85 = gm_l6;
            int64_t r86; bool o87 = __builtin_mul_overflow(t85, ((int64_t)8LL), &r86);
            if (o87) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 119, 11);
            int64_t t88 = r86;
            int64_t t89 = gm_l2;
            uint8_t t90 = (t88 < t89);
            if (!t90) {
                goto gm_lb6;
            }
            {
                int64_t t91 = gm_l6;
                int64_t r92; bool o93 = __builtin_mul_overflow(t91, ((int64_t)2LL), &r92);
                if (o93) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 119, 29);
                int64_t t94 = r92;
                gm_l6 = t94;
            }
gm_lb8:;
        }
    }
gm_lb6:;
    int64_t t95 = gm_l6;
    gm_t913bb735pvt5 t96;
    GM_ST(uint8_t, (uint8_t *)&t96 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t96 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t96 + 12, 0);
    GM_ST(int64_t, ((uint8_t *)&t96), t95);
    ((uint8_t *)&t96)[8] = 1;
    F.ret = t96;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_when_cond_9a46a5h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* cond */
    gm_t5e047ltlbe02 gm_l3; /* v */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t1m5mugfio2m4 gm_l5;
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    void * t6 = gm_l2;
    gm_f_eval_5c9oefa((void *)&gm_l5, (void *)NULL, cx, NULL, t4, t5, t6, ((uint32_t)0ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
    if (t7) {
        memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l5), 24);
    } else {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 8));
        void * t10 = gm_l2;
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s030jgonh2s3s));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(104));
        gm_b0_1 t13 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqs5p4n2tq19v t14;
        memset(&t14, 0, sizeof t14);
        F.ret = t14;
        goto gm_fret;
    }
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
            if (!t16) {
                goto gm_lb2;
            }
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 8));
            gm_l4 = t17;
            uint8_t t18 = gm_l4;
            gm_tqs5p4n2tq19v t19;
            GM_ST(uint8_t, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[1] = 1;
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
        void * t22 = gm_l2;
        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_smbmfla2idsdo));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(67));
        gm_b0_1 t25 = gm_f_report_eardf07((void *)NULL, cx, NULL, t21, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqs5p4n2tq19v t26;
        memset(&t26, 0, sizeof t26);
        F.ret = t26;
        goto gm_fret;
    }
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

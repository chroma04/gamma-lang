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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_b16_8 f11; } gm_th4d8o29g5urm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint64_t f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tkb8hpcqf70t1;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_tjcj4j2gbh6c8 f0; uint8_t f1; uint8_t pe[1]; } gm_tp8mv052i18rg;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th4d8o29g5urm) == 192 && _Alignof(gm_th4d8o29g5urm) == 8 && offsetof(gm_th4d8o29g5urm, f0) == 0 && offsetof(gm_th4d8o29g5urm, f1) == 16 && offsetof(gm_th4d8o29g5urm, f2) == 32 && offsetof(gm_th4d8o29g5urm, f3) == 48 && offsetof(gm_th4d8o29g5urm, f4) == 64 && offsetof(gm_th4d8o29g5urm, f5) == 80 && offsetof(gm_th4d8o29g5urm, f6) == 96 && offsetof(gm_th4d8o29g5urm, f7) == 112 && offsetof(gm_th4d8o29g5urm, f8) == 128 && offsetof(gm_th4d8o29g5urm, f9) == 144 && offsetof(gm_th4d8o29g5urm, f10) == 160 && offsetof(gm_th4d8o29g5urm, f11) == 176, "layout of `(string, string, string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkb8hpcqf70t1) == 24 && _Alignof(gm_tkb8hpcqf70t1) == 8 && offsetof(gm_tkb8hpcqf70t1, f0) == 0 && offsetof(gm_tkb8hpcqf70t1, f1) == 8, "layout of `NamedDecl`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_tkb8hpcqf70t1 f0; uint8_t f1; uint8_t pe[7]; } gm_toblvhepnjqkh;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tp8mv052i18rg) == 6 && _Alignof(gm_tp8mv052i18rg) == 2 && offsetof(gm_tp8mv052i18rg, f0) == 0 && offsetof(gm_tp8mv052i18rg, f1) == 4, "layout of `?(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_toblvhepnjqkh) == 32 && _Alignof(gm_toblvhepnjqkh) == 8 && offsetof(gm_toblvhepnjqkh, f0) == 0 && offsetof(gm_toblvhepnjqkh, f1) == 24, "layout of `?NamedDecl`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_array_len_eielif2 {
    gm_frame h;
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_t5e6kkuq21nig ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_type_64h714c {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_calls_on_value_q0pa6h5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_len_exprs_m8s6m0h {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* names */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_component_type_q9ge5lo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_comptime_value_l5orc93 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_key_of_q36m6nm {
    gm_frame h;
    gm_t5vbkdq81qfs7 l9; /* r */
    gm_t0ialelq3ql1k l10; /* v */
    gm_tbci5ospk5nnn l11; /* i */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_construction_call_ktqtrpo {
    gm_frame h;
    void * ret;
};
struct gm_fr_decl_is_type_oh0cpn9 {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tv2uf4leil5hq l5; /* b */
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_1l2vi9a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_enum_array_type_0sic6t0 {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_ref_7alnaqh {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_tddtjfggdat0d l17; /* have */
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_ty_qaih7ts {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_format_6nmb1bf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bnsf58k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bunqlnl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c3isg16 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_il5kbp4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0aimgq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6tblha {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nbjjmv4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o2s8o9q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o5o5km2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oq3rvor {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pplcoh0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qmamljh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rd9dflp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sv91363 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uhvct7c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_um7orcc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_up1v1ae {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vom8db6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_generic_decl_oir9hm6 {
    gm_frame h;
    gm_toblvhepnjqkh ret;
};
struct gm_fr_generic_type_args_5b8bvov {
    gm_frame h;
    gm_t6art7bprjiqb l5; /* info */
    gm_trqfvmjtt4sij l8; /* given */
    gm_t7te2tdpebbt4 l14; /* a */
    gm_t0mfir12leg2g l16; /* ep */
    gm_tc3oht4ksth9d l17; /* x */
    gm_tc3oht4ksth9d l19; /* x */
    gm_trqfvmjtt4sij l25; /* out */
    gm_t0mfir12leg2g l30; /* ep */
    gm_tc3oht4ksth9d l32; /* g */
    gm_tc3oht4ksth9d l34; /* ex */
    gm_trqfvmjtt4sij l37; /* map */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_hoists_13k2a2s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_type_name_a77a0kf {
    gm_frame h;
    gm_tp8mv052i18rg ret;
};
struct gm_fr_is_builtin_type_name_v5ttoka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_oversized_int_name_nr25haf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_path_qa34i5j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_len_param_names_cilm68f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_len_params_bound_f7mjvn9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_length_is_type_soi53ah {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9tqbks7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_const_idpokjh {
    gm_frame h;
    gm_tk1gen3hgkm51 ret;
};
struct gm_fr_local_fn_aji8qnd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_looks_like_type_ho5qv1k {
    gm_frame h;
    gm_tv1o1j44ii4db l11; /* el */
    uint8_t ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_names_error_oet8t6b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_names_type_rjdgib9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prefix_operand_3n5jchh {
    gm_frame h;
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_afk8dfq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_down_hge098p {
    gm_frame h;
    gm_t2gtbh39ptekd l21; /* kind */
    void * ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hv483nm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k0n7eot {
    gm_frame h;
    gm_t0mfir12leg2g l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pushes_down_0djaflp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0ui7njc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_12ta4fr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lhe5gv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_type_jjodbei {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_type_unchecked_psme7dn {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* ts */
    gm_tv1o1j44ii4db l16; /* el */
    gm_trqfvmjtt4sij l17; /* names */
    gm_tv1o1j44ii4db l20; /* el */
    gm_tqqhoivahirr0 l30; /* f */
    gm_tqqhoivahirr0 l48; /* f */
    gm_tkacitepg5fk0 l78; /* a */
    gm_tet9guhev5r4u l80; /* i */
    gm_tc3oht4ksth9d l82; /* x */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_syntax_is_type_pe6ugv2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_syntax_module_vho1001 {
    gm_frame h;
    gm_t6qooom8qa2do l9; /* v */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
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
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_param_3av4r8i {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unknown_type_beat5io {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_trqfvmjtt4sij l7; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_param_tof1h6t {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_with_operand_ajbkgld {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lhe5gv6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_type_beat5io(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_k027ibn62fv[40] __attribute__((aligned(8)));
extern uint8_t gm_k647t05rm86[40] __attribute__((aligned(8)));
extern uint8_t gm_kcodas3apkm[40] __attribute__((aligned(8)));
extern uint8_t gm_knjqrvnblc8[40] __attribute__((aligned(8)));
extern uint8_t gm_ks263t40fiu[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0hcm1974rbe7[34];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s2328gjukbu28[16];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s43j3q24pq7de[26];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s5veo86cvpdae[86];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6hgf366ivqrg[34];
extern const uint8_t gm_s6i2dst792b3t[54];
extern const uint8_t gm_s6rj805fm39qm[65];
extern const uint8_t gm_s783a88c5t0ve[67];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9rkpo2ljmui9[50];
extern const uint8_t gm_sbfpg6hac36v9[19];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_se5118fq98e06[88];
extern const uint8_t gm_sencmbgnvrve5[18];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgva3apqq9jo2[68];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_sjik6bsmdi5g2[83];
extern const uint8_t gm_sk5d5fbcb3pir[10];
extern const uint8_t gm_skpsvuvlfj0ep[68];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_sldikqa9i70sm[52];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_sm9funvq1222p[44];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_sn790bmojc3gl[33];
extern const uint8_t gm_sobb0dbnbb0kc[56];
extern const uint8_t gm_spdcqn4vu2cam[45];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_ss8fufid8t1o1[32];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_ssv8t6tsqrjjf[97];
extern const uint8_t gm_stevggi5tvq7c[18];
extern const uint8_t gm_su6il7ugmnq2g[48];
extern const gm_tmpl gm_tpl0vock46lp3ec;
extern const gm_tmpl gm_tpl2fcbm03ka11j;
extern const gm_tmpl gm_tpl5cnv021ebod7;
extern const gm_tmpl gm_tpl8v9uhia7uc1b;
extern const gm_tmpl gm_tpla0ci4vs242iu;
extern const gm_tmpl gm_tplc1tlvqjnir84;
extern const gm_tmpl gm_tpld3o5iqghffqq;
extern const gm_tmpl gm_tpleeuqgcr11ngn;
extern const gm_tmpl gm_tpleo7934k1qvrc;
extern const gm_tmpl gm_tplg21ilonldhva;
extern const gm_tmpl gm_tplhfdokrq767sh;
extern const gm_tmpl gm_tplj4gqj9p0fguq;
extern const gm_tmpl gm_tpljt2av4h7c132;
extern const gm_tmpl gm_tplk0jab62rgl3v;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tplmf8455dp92a3;
extern const gm_tmpl gm_tpln92q8vfbc4aq;
extern const gm_tmpl gm_tplup0bf5d2eopf;
extern const gm_tmpl gm_tplv1cj36rb6m0h;
extern const gm_tmpl gm_tplv23k7silrvnp;
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
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)4ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_calls_on_value_q0pa6h5 F;
    void * gm_l0; /* callee */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)22ULL));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t9;
            void * t10 = gm_l1;
            uint8_t t11 = gm_f_is_path_qa34i5j(t10);
            uint8_t t12 = (uint8_t)!t11;
            gm_l2 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)20ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 270, 58);
    }
gm_lb1:;
    uint8_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 190, 14);
    void * t7 = (void *)(((uint8_t *)t4) + (uintptr_t)t6 * 216);
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
    void * t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
    uint64_t t13 = gm_l2;
    uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_tf5srh4mp4b3i t4 = gm_l2;
    gm_b16_8 t5 = gm_l3;
    void * t6 = gm_l4;
    gm_t88rcucg1b7uk t7 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1l2vi9a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 28)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1688));
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 1728));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1 + 0), t4);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1 + 40), t5);
    }
    {
        void * t6 = gm_l0;
        gm_trqfvmjtt4sij t7;
        gm_f_list_1jabh22((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t6) + 1688), t7);
    }
    {
        void * t8 = gm_l0;
        gm_trqfvmjtt4sij t9;
        gm_f_list_0bj26n7((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 1728), t9);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6nmb1bf F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6hgf366ivqrg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5cnv021ebod7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bnsf58k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn790bmojc3gl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleo7934k1qvrc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bunqlnl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sobb0dbnbb0kc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljt2av4h7c132), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c3isg16 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6i2dst792b3t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleeuqgcr11ngn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_il5kbp4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5veo86cvpdae));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(85));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0vock46lp3ec), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0aimgq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su6il7ugmnq2g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpld3o5iqghffqq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6tblha F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s43j3q24pq7de));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj4gqj9p0fguq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nbjjmv4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0hcm1974rbe7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplup0bf5d2eopf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o2s8o9q F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjik6bsmdi5g2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(82));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv1cj36rb6m0h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o5o5km2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6rj805fm39qm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8v9uhia7uc1b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_oq3rvor F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s783a88c5t0ve));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv23k7silrvnp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pplcoh0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sencmbgnvrve5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhfdokrq767sh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qmamljh F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se5118fq98e06));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(87));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmf8455dp92a3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rd9dflp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skpsvuvlfj0ep));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla0ci4vs242iu), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sv91363 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbfpg6hac36v9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln92q8vfbc4aq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uhvct7c F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss8fufid8t1o1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk0jab62rgl3v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_um7orcc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssv8t6tsqrjjf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(96));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2fcbm03ka11j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_up1v1ae F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spdcqn4vu2cam));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc1tlvqjnir84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vom8db6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk5d5fbcb3pir));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg21ilonldhva), t2);
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_hoists_13k2a2s(t2);
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    void * t6 = gm_l0;
    void * t7 = gm_f_prefix_operand_3n5jchh(t6);
    void * t8 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t5, t7);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)26ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)26ULL));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_path_qa34i5j F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)6ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)22ULL));
            if (!t10) {
                goto gm_lb3;
            }
            void * t11 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t11;
            void * t12 = gm_l1;
            uint8_t t13 = gm_f_is_path_qa34i5j(t12);
            gm_l2 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 276, 46);
    }
gm_lb1:;
    uint8_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memmove((((uint8_t *)t2) + 1688), ((uint8_t *)a1), 40);
    }
    {
        void * t3 = gm_l0;
        memmove((((uint8_t *)t3) + 1728), (((uint8_t *)a1) + 40), 40);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9tqbks7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1l2vi9a();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 256));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 256) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_afk8dfq F;
    void * gm_l0; /* l */
    gm_t8er5olt969ip gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d3uqusp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0ui7njc(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 28), ((uint8_t *)a1), 28);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 28)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hv483nm F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_osbc35j();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_12ta4fr(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k0n7eot F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1l2vi9a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lhe5gv6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 96), ((uint8_t *)&F.l1), 96);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    gm_b16_8 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_tep2drsl9408b t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcsffver18si54, 63, 5);
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0ui7njc F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)28ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_12ta4fr F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lhe5gv6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lhe5gv6 F;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tgglga8ro3isc t5;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1608) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1648) + 8));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 1728) + 8));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1688) + 8));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8), t5);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16), t7);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24), t9);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t2, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)6ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_array_len_eielif2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint32_t gm_l4; /* pid */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint64_t gm_l8; /* n */
    void * gm_l9; /* hook */
    GM_STACK_CHECK(&F, 1157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tkv1vap90vht2 gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    {
        {
            void * t16 = gm_l2;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)6ULL));
            if (!t18) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t16) + 8) + 8), 32);
            {
                {
                    void * t19 = gm_l0;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_t9805qtonbsag t21 = gm_f_value_param_tof1h6t(t19, t20);
                    gm_l10 = t21;
                    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                    if (!t22) {
                        goto gm_lb4;
                    }
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                    gm_l4 = t23;
                    {
                        {
                            void * t24 = gm_l0;
                            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 1128));
                            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 1128) + 8));
                            uint32_t t27 = gm_l4;
                            uint64_t t28 = (uint64_t)t27;
                            if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srcsffver18si54, 626, 26);
                            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 0));
                            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)2ULL));
                            if (!t30) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 4));
                            gm_l5 = t31;
                            void * t32 = gm_l0;
                            void * t33 = (void *)(((uint8_t *)t32) + 8);
                            gm_t88rcucg1b7uk t34 = gm_l5;
                            uint8_t t35 = gm_f_is_integer_8d4f9ek(t33, GM_CONV(gm_csguankko4lmhd, t34));
                            uint8_t t36 = (uint8_t)!t35;
                            if (t36) {
                                void * t37 = gm_l0;
                                void * t38 = gm_l2;
                                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                                gm_tnpifv20irnph t41;
                                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                                gm_f_format_bunqlnl((void *)&gm_l11, (void *)NULL, cx, NULL, t41);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 628, 33);
                                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                                gm_b0_1 t45 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t37, t39, t44);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t5e6kkuq21nig t46;
                                GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
                                F.ret = t46;
                                goto gm_fret;
                            }
                            void * t47 = gm_l0;
                            void * t48 = (void *)(((uint8_t *)t47) + 8);
                            uint32_t t49 = gm_l4;
                            gm_t88rcucg1b7uk t50 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t5e6kkuq21nig t51;
                            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t51 + 8), t50);
                            F.ret = t51;
                            goto gm_fret;
                        }
gm_lb6:;
                        gm_t5e6kkuq21nig t52;
                        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t52 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)2ULL));
                        F.ret = t52;
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t53 = gm_l0;
    uint64_t t54 = gm_l1;
    void * t55 = gm_l2;
    gm_f_try_untyped_fj0h8i5((void *)&gm_l12, (void *)NULL, cx, NULL, t53, t54, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (!t56) {
                goto gm_lb8;
            }
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
            if (!t58) {
                goto gm_lb8;
            }
            {
                memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l12) + 8), 200);
            }
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                    if (!t60) {
                        goto gm_lb10;
                    }
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 48);
                    }
                    {
                        {
                            gm_tf5srh4mp4b3i t61 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                            gm_l13 = t61;
                            uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                            if (!t62) {
                                goto gm_lb12;
                            }
                            uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                            gm_l8 = t63;
                            uint64_t t64 = gm_l8;
                            gm_t5e6kkuq21nig t65;
                            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t65 + 8), t64);
                            F.ret = t65;
                            goto gm_fret;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    {
                        void * t66 = gm_l0;
                        void * t67 = gm_l2;
                        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                        gm_b16_8 t69 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t70;
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                        gm_f_format_uhvct7c((void *)&gm_l14, (void *)NULL, cx, NULL, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcsffver18si54, 638, 33);
                        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                        gm_b0_1 t74 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t66, t68, t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_t5e6kkuq21nig t75;
                    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                    F.ret = t75;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            {
                void * t76 = gm_l0;
                void * t77 = gm_l2;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                void * t79 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t80 = gm_f_describe_bm129vh(t79);
                gm_tnpifv20irnph t81;
                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
                gm_f_format_up1v1ae((void *)&gm_l15, (void *)NULL, cx, NULL, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcsffver18si54, 641, 29);
                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                gm_b0_1 t85 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t76, t78, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t5e6kkuq21nig t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
            F.ret = t86;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (!t87) {
                goto gm_lb13;
            }
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)1ULL));
            if (!t89) {
                goto gm_lb13;
            }
            gm_t5e6kkuq21nig t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)2ULL));
            F.ret = t90;
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (t91) {
                goto gm_lb14;
            }
            void * t92 = gm_l0;
            uint8_t t93 = (uint8_t)(GM_LD(void *, (((uint8_t *)t92) + 2296)) != 0);
            if (t93) {
                void * t94 = GM_LD(void *, (((uint8_t *)t92) + 2296));
                gm_l9 = t94;
            } else {
                void * t95 = gm_l0;
                void * t96 = gm_l2;
                gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s9rkpo2ljmui9));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(49));
                gm_b0_1 t99 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t95, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5e6kkuq21nig t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)2ULL));
                F.ret = t100;
                goto gm_fret;
            }
            void * t101 = gm_l9;
            void * t102 = gm_l0;
            uint64_t t103 = gm_l1;
            void * t104 = gm_l2;
            void * t105 = t101;
            gm_t5e6kkuq21nig t106;
            if (GM_PTR_TAGGED(t105)) {
                t106 = ((gm_t5e6kkuq21nig (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t105))(t102, t103, t104);
            } else {
                t106 = ((gm_t5e6kkuq21nig (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t105)(NULL, cx, NULL, t102, t103, t104);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t106;
            goto gm_fret;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 634, 5);
    }
gm_lb7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_builtin_type_64h714c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjcj4j2gbh6c8 gm_l2; /* i */
    GM_STACK_CHECK(&F, 1164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tp8mv052i18rg gm_l3;
    gm_tkn42u7na3t81 gm_l4;
    {
        {
            gm_b16_8 t18 = gm_l1;
            gm_tp8mv052i18rg t19 = gm_f_int_type_name_a77a0kf(t18);
            gm_l3 = t19;
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_tjcj4j2gbh6c8 t21 = GM_LD(gm_tjcj4j2gbh6c8, ((uint8_t *)&gm_l3));
            gm_l2 = t21;
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            uint16_t t25 = GM_LD(uint16_t, (((uint8_t *)&gm_l2) + 2));
            gm_t88rcucg1b7uk t26 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[4] = 1;
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            gm_b16_8 t28 = gm_l1;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s6groq37ram3h));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
            uint8_t t30 = (gm_eq0iqiubfm7e(((uint8_t *)&t28), ((uint8_t *)&t29)));
            if (!t30) {
                goto gm_lb4;
            }
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            gm_t88rcucg1b7uk t33 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[4] = 1;
            gm_l4 = t34;
            goto gm_lb3;
        }
gm_lb4:;
        {
            gm_b16_8 t35 = gm_l1;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s0i3i86nphpsh));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(5));
            uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
            if (!t37) {
                goto gm_lb5;
            }
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_t53at80pfdus3 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t41 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t39, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[4] = 1;
            gm_l4 = t42;
            goto gm_lb3;
        }
gm_lb5:;
        {
            gm_b16_8 t43 = gm_l1;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(3));
            uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
            if (!t45) {
                goto gm_lb6;
            }
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 8);
            gm_t53at80pfdus3 t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t48 + 8), ((uint16_t)16ULL));
            gm_t88rcucg1b7uk t49 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t47, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[4] = 1;
            gm_l4 = t50;
            goto gm_lb3;
        }
gm_lb6:;
        {
            gm_b16_8 t51 = gm_l1;
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
            uint8_t t53 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t52)));
            if (!t53) {
                goto gm_lb7;
            }
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            gm_t53at80pfdus3 t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t56 + 8), ((uint16_t)32ULL));
            gm_t88rcucg1b7uk t57 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t55, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t58), t57);
            ((uint8_t *)&t58)[4] = 1;
            gm_l4 = t58;
            goto gm_lb3;
        }
gm_lb7:;
        {
            gm_b16_8 t59 = gm_l1;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
            uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
            if (!t61) {
                goto gm_lb8;
            }
            void * t62 = gm_l0;
            void * t63 = (void *)(((uint8_t *)t62) + 8);
            gm_t53at80pfdus3 t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t64 + 8), ((uint16_t)64ULL));
            gm_t88rcucg1b7uk t65 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t63, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66), t65);
            ((uint8_t *)&t66)[4] = 1;
            gm_l4 = t66;
            goto gm_lb3;
        }
gm_lb8:;
        {
            gm_b16_8 t67 = gm_l1;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s929lennfd3g6));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
            uint8_t t69 = (gm_eq0iqiubfm7e(((uint8_t *)&t67), ((uint8_t *)&t68)));
            if (!t69) {
                goto gm_lb9;
            }
            void * t70 = gm_l0;
            void * t71 = (void *)(((uint8_t *)t70) + 8);
            gm_t88rcucg1b7uk t72 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t73), t72);
            ((uint8_t *)&t73)[4] = 1;
            gm_l4 = t73;
            goto gm_lb3;
        }
gm_lb9:;
        {
            gm_b16_8 t74 = gm_l1;
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s3cv29r83pgn4));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(5));
            uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
            if (!t76) {
                goto gm_lb10;
            }
            void * t77 = gm_l0;
            void * t78 = (void *)(((uint8_t *)t77) + 8);
            gm_t88rcucg1b7uk t79 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t80), t79);
            ((uint8_t *)&t80)[4] = 1;
            gm_l4 = t80;
            goto gm_lb3;
        }
gm_lb10:;
        {
            gm_b16_8 t81 = gm_l1;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(4));
            uint8_t t83 = (gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
            if (!t83) {
                goto gm_lb11;
            }
            void * t84 = gm_l0;
            void * t85 = (void *)(((uint8_t *)t84) + 8);
            gm_t88rcucg1b7uk t86 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87), t86);
            ((uint8_t *)&t87)[4] = 1;
            gm_l4 = t87;
            goto gm_lb3;
        }
gm_lb11:;
        {
            gm_b16_8 t88 = gm_l1;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_slolcr6ikvg33));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
            uint8_t t90 = (gm_eq0iqiubfm7e(((uint8_t *)&t88), ((uint8_t *)&t89)));
            if (!t90) {
                goto gm_lb12;
            }
            void * t91 = gm_l0;
            void * t92 = (void *)(((uint8_t *)t91) + 8);
            gm_t53at80pfdus3 t93;
            memset(&t93, 0, sizeof t93);
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)7ULL));
            gm_t88rcucg1b7uk t94 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t92, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t95), t94);
            ((uint8_t *)&t95)[4] = 1;
            gm_l4 = t95;
            goto gm_lb3;
        }
gm_lb12:;
        {
            gm_b16_8 t96 = gm_l1;
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(6));
            uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
            if (!t98) {
                goto gm_lb13;
            }
            void * t99 = gm_l0;
            void * t100 = (void *)(((uint8_t *)t99) + 8);
            gm_t88rcucg1b7uk t101 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t102), t101);
            ((uint8_t *)&t102)[4] = 1;
            gm_l4 = t102;
            goto gm_lb3;
        }
gm_lb13:;
        {
            gm_b16_8 t103 = gm_l1;
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(7));
            uint8_t t105 = (gm_eq0iqiubfm7e(((uint8_t *)&t103), ((uint8_t *)&t104)));
            if (!t105) {
                goto gm_lb14;
            }
            void * t106 = gm_l0;
            void * t107 = (void *)(((uint8_t *)t106) + 8);
            void * t108 = gm_l0;
            void * t109 = (void *)(((uint8_t *)t108) + 8);
            gm_t88rcucg1b7uk t110 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t111 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t107, ((uint8_t)1), t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112), t111);
            ((uint8_t *)&t112)[4] = 1;
            gm_l4 = t112;
            goto gm_lb3;
        }
gm_lb14:;
        {
            gm_b16_8 t113 = gm_l1;
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(4));
            uint8_t t115 = (gm_eq0iqiubfm7e(((uint8_t *)&t113), ((uint8_t *)&t114)));
            if (!t115) {
                goto gm_lb15;
            }
            void * t116 = gm_l0;
            void * t117 = (void *)(((uint8_t *)t116) + 8);
            gm_t88rcucg1b7uk t118 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t117, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t119), t118);
            ((uint8_t *)&t119)[4] = 1;
            gm_l4 = t119;
            goto gm_lb3;
        }
gm_lb15:;
        {
            gm_tkn42u7na3t81 t120;
            memset(&t120, 0, sizeof t120);
            gm_l4 = t120;
            goto gm_lb3;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 31, 12);
    }
gm_lb3:;
    gm_tkn42u7na3t81 t121 = gm_l4;
    F.ret = t121;
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
extern __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2) {
    struct gm_fr_check_len_exprs_m8s6m0h F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* map */
    gm_toceffb697p0u gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t2i8ne5ibd7v4 gm_l6; /* le */
    gm_tl874t2atnlh3 gm_l9; /* a */
    GM_STACK_CHECK(&F, 814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_l4 = ((uint64_t)0ULL);
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 2312) + 8));
    gm_l3 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = gm_l3;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l4;
                        gm_l5 = t14;
                        void * t15 = gm_l0;
                        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 2312));
                        uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 2312) + 8));
                        uint64_t t18 = gm_l5;
                        if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcsffver18si54, 660, 15);
                        gm_t2i8ne5ibd7v4 t19 = GM_LD(gm_t2i8ne5ibd7v4, (((uint8_t *)t16) + (uintptr_t)t18 * 16));
                        gm_l6 = t19;
                        void * t20 = gm_l0;
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = gm_f_len_params_bound_f7mjvn9(t20, GM_CONV(gm_csguankko4lmhd, t21), t22);
                        uint8_t t24 = (uint8_t)!t23;
                        if (t24) {
                            gm_l10 = ((uint8_t)1);
                        } else {
                            void * t25 = gm_l0;
                            void * t26 = (void *)(((uint8_t *)t25) + 8);
                            void * t27 = gm_l0;
                            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                            gm_b16_8 t29 = gm_l1;
                            gm_t88rcucg1b7uk t30 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t27, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t31 = gm_f_is_error_8k27gd1(t26, GM_CONV(gm_csguankko4lmhd, t30));
                            uint8_t t32 = (uint8_t)!t31;
                            gm_l10 = t32;
                        }
                        uint8_t t33 = gm_l10;
                        if (t33) {
                            goto gm_lb3;
                        }
                        {
                            gm_trqfvmjtt4sij t34;
                            gm_f_list_epvducg((void *)&t34, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l7 = t34;
                        }
                        void * t35 = gm_l0;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                        gm_b16_8 t37 = gm_l1;
                        void * t38 = (void *)((uint8_t *)&F.l7);
                        gm_b0_1 t39 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t35, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
                            void * t41 = gm_l0;
                            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                            gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                            gm_b16_8 t46;
                            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
                            gm_f_join_sfknljb((void *)&gm_l11, (void *)NULL, cx, NULL, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 664, 129);
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                            gm_t6j6smj7qoctu t49;
                            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t43);
                            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t48);
                            gm_f_format_rd9dflp((void *)&gm_l12, (void *)NULL, cx, NULL, &t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 664, 34);
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                            gm_t4gut2c9fj1kt t53;
                            gm_f_error_s6hn871((void *)&t53, (void *)NULL, cx, NULL, t40, t52);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l8 = t53;
                        }
                        {
                            {
                                uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l2))[12] != 0);
                                if (!t54) {
                                    goto gm_lb7;
                                }
                                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l2));
                                gm_l9 = t55;
                                void * t56 = (void *)((uint8_t *)&F.l8);
                                gm_tl874t2atnlh3 t57 = gm_l9;
                                gm_b16_8 t58;
                                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_stevggi5tvq7c));
                                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(17));
                                gm_f_add_label_50fj7vv(t56, GM_CONV(gm_cseahvg8v7p0vs, t57), t58);
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            void * t59 = gm_l0;
                            gm_b0_1 t60 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t59, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 659, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l4;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l4 = t64;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_component_type_q9ge5lo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l5;
    gm_t53at80pfdus3 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    void * t7 = gm_l2;
    gm_t88rcucg1b7uk t8 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_t88rcucg1b7uk t11 = gm_l3;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t10, GM_CONV(gm_csguankko4lmhd, t11));
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)15ULL));
            if (!t13) {
                goto gm_lb2;
            }
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 538, 8);
    }
gm_lb1:;
    uint8_t t14 = gm_l5;
    if (t14) {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l3;
        gm_b16_8 t17 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t17;
        {
            void * t18 = gm_l0;
            void * t19 = gm_l2;
            gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
            gm_b16_8 t21 = gm_l4;
            gm_b16_8 t22 = gm_l4;
            gm_t6j6smj7qoctu t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
            gm_f_format_um7orcc((void *)&gm_l7, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcsffver18si54, 540, 25);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_b0_1 t27 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t18, t20, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    gm_t88rcucg1b7uk t28 = gm_l3;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_comptime_value_l5orc93 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* e */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint32_t gm_l5; /* pid */
    gm_t88rcucg1b7uk gm_l6; /* have */
    gm_t428ue2u3hf5g gm_l7; /* key */
    void * gm_l8; /* hook */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* cell */
    gm_t428ue2u3hf5g gm_l11; /* key */
    GM_STACK_CHECK(&F, 1255u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_trgmlgt2gmmjs gm_l14;
    gm_t9805qtonbsag gm_l15;
    gm_trgmlgt2gmmjs gm_l16;
    {
        {
            void * t14 = gm_l3;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)6ULL));
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t14) + 8) + 8), 32);
            {
                {
                    void * t17 = gm_l0;
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_t9805qtonbsag t19 = gm_f_value_param_tof1h6t(t17, t18);
                    gm_l12 = t19;
                    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
                    if (!t20) {
                        goto gm_lb4;
                    }
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                    gm_l5 = t21;
                    {
                        {
                            void * t22 = gm_l0;
                            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 1128));
                            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 1128) + 8));
                            uint32_t t25 = gm_l5;
                            uint64_t t26 = (uint64_t)t25;
                            if ((uintptr_t)t26 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t26, t24, gm_srcsffver18si54, 792, 29);
                            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t23) + (uintptr_t)t26 * 48) + 0));
                            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
                            if (!t28) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t23) + (uintptr_t)t26 * 48) + 4));
                            gm_l6 = t29;
                            {
                                {
                                    gm_t88rcucg1b7uk t30 = gm_l6;
                                    gm_t88rcucg1b7uk t31 = gm_l2;
                                    uint8_t t32 = (!gm_eq9m6mvtmgde(((uint8_t *)&t30), ((uint8_t *)&t31)));
                                    if (!t32) {
                                        goto gm_lb8;
                                    }
                                    gm_t88rcucg1b7uk t33 = gm_l2;
                                    void * t34 = gm_l0;
                                    void * t35 = (void *)(((uint8_t *)t34) + 8);
                                    gm_t88rcucg1b7uk t36 = gm_l6;
                                    gm_t88rcucg1b7uk t37 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t35, t36);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t38 = (!gm_eq9m6mvtmgde(((uint8_t *)&t33), ((uint8_t *)&t37)));
                                    if (!t38) {
                                        goto gm_lb8;
                                    }
                                    {
                                        void * t39 = gm_l0;
                                        void * t40 = gm_l3;
                                        gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                                        void * t42 = gm_l0;
                                        gm_t88rcucg1b7uk t43 = gm_l2;
                                        gm_b16_8 t44 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t42, t43);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                        void * t46 = gm_l0;
                                        gm_t88rcucg1b7uk t47 = gm_l6;
                                        gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tfeab279c16rr t49;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t44);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t45);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 32), t48);
                                        gm_f_format_c3isg16((void *)&gm_l13, (void *)NULL, cx, NULL, &t49);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcsffver18si54, 794, 33);
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                                        gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t39, t41, t52);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    gm_tkn42u7na3t81 t54;
                                    memset(&t54, 0, sizeof t54);
                                    F.ret = t54;
                                    goto gm_fret;
                                }
gm_lb8:;
                            }
gm_lb7:;
                            void * t55 = gm_l0;
                            void * t56 = (void *)(((uint8_t *)t55) + 8);
                            uint32_t t57 = gm_l5;
                            gm_t88rcucg1b7uk t58 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t56, t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tkn42u7na3t81 t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59), t58);
                            ((uint8_t *)&t59)[4] = 1;
                            F.ret = t59;
                            goto gm_fret;
                        }
gm_lb6:;
                        gm_tkn42u7na3t81 t60;
                        memset(&t60, 0, sizeof t60);
                        F.ret = t60;
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t61 = gm_l0;
                uint64_t t62 = gm_l1;
                void * t63 = gm_l3;
                gm_t88rcucg1b7uk t64 = gm_l2;
                gm_f_const_key_of_q36m6nm((void *)&gm_l14, (void *)NULL, cx, NULL, t61, t62, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l14))[32] != 0);
            if (!t65) {
                goto gm_lb10;
            }
            {
                memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l14), 32);
            }
            {
                void * t66 = gm_l0;
                void * t67 = (void *)(((uint8_t *)t66) + 8);
                gm_t428ue2u3hf5g t68 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l7));
                gm_t53at80pfdus3 t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t69 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)21ULL));
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t69 + 8), t68);
                gm_t88rcucg1b7uk t70 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t67, &t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t71;
                GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t71), t70);
                ((uint8_t *)&t71)[4] = 1;
                F.ret = t71;
                goto gm_fret;
            }
        }
gm_lb10:;
    }
gm_lb9:;
    {
        {
            void * t72 = gm_l0;
            uint8_t t73 = (uint8_t)(GM_LD(void *, (((uint8_t *)t72) + 2288)) != 0);
            if (!t73) {
                goto gm_lb12;
            }
            void * t74 = GM_LD(void *, (((uint8_t *)t72) + 2288));
            gm_l8 = t74;
            void * t75 = gm_l0;
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t88rcucg1b7uk t77 = gm_l2;
            uint8_t t78 = gm_f_has_param_u9t0efs(t76, GM_CONV(gm_csguankko4lmhd, t77));
            uint8_t t79 = (uint8_t)!t78;
            if (!t79) {
                goto gm_lb12;
            }
            void * t80 = gm_l8;
            void * t81 = gm_l0;
            uint64_t t82 = gm_l1;
            void * t83 = gm_l3;
            gm_t88rcucg1b7uk t84 = gm_l2;
            gm_tkn42u7na3t81 t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85), t84);
            ((uint8_t *)&t85)[4] = 1;
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s33hvuvcdjs1b));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(10));
            void * t87 = gm_l3;
            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
            void * t89 = t80;
            gm_t9805qtonbsag t90;
            if (GM_PTR_TAGGED(t89)) {
                t90 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t89))(t81, t82, t83, t85, t86, GM_CONV(gm_cseahvg8v7p0vs, t88));
            } else {
                t90 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t89)(NULL, cx, NULL, t81, t82, t83, t85, t86, t88);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t90;
            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
            if (t91) {
                uint32_t t92 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
                gm_l9 = t92;
            } else {
                gm_tkn42u7na3t81 t93;
                memset(&t93, 0, sizeof t93);
                F.ret = t93;
                goto gm_fret;
            }
            void * t94 = gm_l0;
            void * t95 = GM_LD(void *, (((uint8_t *)t94) + 2088));
            uint64_t t96 = GM_LD(uint64_t, ((((uint8_t *)t94) + 2088) + 8));
            uint32_t t97 = gm_l9;
            uint64_t t98 = (uint64_t)t97;
            if ((uintptr_t)t98 >= (uintptr_t)(t96)) gm_trap_index((uintptr_t)t98, t96, gm_srcsffver18si54, 803, 17);
            uint8_t t99 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t95) + (uintptr_t)t98 * 88) + 80)) != 0);
            if (t99) {
                void * t100 = GM_LD(void *, ((((uint8_t *)t95) + (uintptr_t)t98 * 88) + 80));
                gm_l10 = t100;
            } else {
                gm_tkn42u7na3t81 t101;
                memset(&t101, 0, sizeof t101);
                F.ret = t101;
                goto gm_fret;
            }
            {
                void * t102 = gm_l0;
                void * t103 = gm_l10;
                gm_t88rcucg1b7uk t104 = gm_l2;
                void * t105 = gm_l3;
                gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 72));
                gm_t6evimt84lndq t107 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t103));
                gm_f_value_key_vki574i((void *)&gm_l16, (void *)NULL, cx, NULL, t102, &t107, t104, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t108 = (uint8_t)((((uint8_t *)&gm_l16))[32] != 0);
            if (t108) {
                memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l16), 32);
            } else {
                gm_tkn42u7na3t81 t109;
                memset(&t109, 0, sizeof t109);
                F.ret = t109;
                goto gm_fret;
            }
            {
                void * t110 = gm_l0;
                void * t111 = (void *)(((uint8_t *)t110) + 8);
                gm_t428ue2u3hf5g t112 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l11));
                gm_t53at80pfdus3 t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)21ULL));
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t113 + 8), t112);
                gm_t88rcucg1b7uk t114 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t111, &t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t115;
                GM_ST(uint8_t, (uint8_t *)&t115 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t115 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t115), t114);
                ((uint8_t *)&t115)[4] = 1;
                F.ret = t115;
                goto gm_fret;
            }
        }
gm_lb12:;
    }
gm_lb11:;
    void * t116 = gm_l0;
    void * t117 = gm_l3;
    gm_tl874t2atnlh3 t118 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t117) + 72));
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sm7179tmr0nhi));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(26));
    gm_b0_1 t120 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t116, t118, t119);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t121;
    memset(&t121, 0, sizeof t121);
    F.ret = t121;
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
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_key_of_q36m6nm F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* x */
    void * gm_l7; /* inner */
    gm_tibupt4dmcqai gm_l8; /* lit */
    uint8_t gm_l12; /* b */
    GM_STACK_CHECK(&F, 1273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l14;
    gm_t9805qtonbsag gm_l15;
    gm_tkv1vap90vht2 gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tftb1odc739t5 gm_l19;
    {
        void * t17 = gm_l2;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
        uint8_t t19 = (uint8_t)((t18 == ((uint8_t)4ULL)));
        gm_t2gtbh39ptekd t20 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t17) + 8));
        gm_t2gtbh39ptekd t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)4ULL));
        if (t19) {
            {
                {
                    {
                        void * t22 = gm_l0;
                        void * t23 = (void *)(((uint8_t *)t22) + 8);
                        gm_t88rcucg1b7uk t24 = gm_l3;
                        gm_f_kind_jcnt4hm((void *)&gm_l14, t23, GM_CONV(gm_csguankko4lmhd, t24));
                    }
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)13ULL));
                    if (!t26) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                    gm_l4 = t27;
                    void * t28 = gm_l0;
                    gm_t88rcucg1b7uk t29 = gm_l4;
                    uint8_t t30 = gm_f_is_nullable_pointer_nkie06s(t28, GM_CONV(gm_csguankko4lmhd, t29));
                    if (!t30) {
                        goto gm_lb2;
                    }
                    {
                        gm_t428ue2u3hf5g t31;
                        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t31);
                        ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
gm_lb1:;
            memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
            goto gm_fdone;
        }
    }
    {
        {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            void * t34 = gm_l2;
            gm_t88rcucg1b7uk t35 = gm_l3;
            gm_t9805qtonbsag t36 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t32, t33, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t36;
            uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
            if (!t37) {
                goto gm_lb4;
            }
            uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
            gm_l5 = t38;
            {
                uint32_t t39 = gm_l5;
                void * t40 = gm_l0;
                void * t41 = GM_LD(void *, (((uint8_t *)t40) + 344));
                uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 344) + 8));
                uint32_t t43 = gm_l5;
                uint64_t t44 = (uint64_t)t43;
                if ((uintptr_t)t44 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t44, t42, gm_srcsffver18si54, 818, 53);
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t41) + (uintptr_t)t44 * 136));
                gm_tiilmfd16v3fc t46;
                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t39);
                GM_ST(gm_b16_8, ((uint8_t *)&t46 + 8), t45);
                gm_t428ue2u3hf5g t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t47 + 8), t46);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t47);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb4:;
    }
gm_lb3:;
    void * t48 = gm_l0;
    void * t49 = (void *)(((uint8_t *)t48) + 8);
    gm_t88rcucg1b7uk t50 = gm_l3;
    uint8_t t51 = gm_f_has_param_u9t0efs(t49, GM_CONV(gm_csguankko4lmhd, t50));
    if (t51) {
        memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
        goto gm_fdone;
    }
    void * t52 = gm_l2;
    gm_l6 = t52;
    {
        for (;;) {
            void * t53 = gm_l6;
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)9ULL));
            if (!t55) {
                goto gm_lb5;
            }
            void * t56 = GM_LD(void *, ((((uint8_t *)t53) + 8) + 8));
            gm_l7 = t56;
            {
                void * t57 = gm_l7;
                gm_l6 = t57;
            }
gm_lb7:;
        }
    }
gm_lb5:;
    {
        {
            {
                void * t58 = gm_l6;
                uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t58) + 8) + 0));
                uint8_t t60 = (uint8_t)(t59 == ((uint8_t)2ULL));
                if (!t60) {
                    goto gm_lb9;
                }
                memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t58) + 8) + 8), 24);
            }
            gm_t9qkh0rsooc8d t61 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 16));
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
            uint8_t t63 = (uint8_t)(!(t62 == ((uint8_t)2ULL)));
            if (!t63) {
                goto gm_lb9;
            }
            gm_t88rcucg1b7uk t64 = gm_l3;
            void * t65 = gm_l0;
            void * t66 = (void *)(((uint8_t *)t65) + 8);
            gm_t9qkh0rsooc8d t67;
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t68 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = (gm_eq9m6mvtmgde(((uint8_t *)&t64), ((uint8_t *)&t68)));
            if (!t69) {
                goto gm_lb9;
            }
            {
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_t428ue2u3hf5g t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)3ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t72);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb9:;
    }
gm_lb8:;
    {
        void * t73 = gm_l0;
        uint64_t t74 = gm_l1;
        void * t75 = gm_l2;
        gm_f_untyped_if_plain_lbrp24p((void *)&gm_l16, (void *)NULL, cx, NULL, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l16))[208] != 0);
    if (t76) {
        memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l16), 208);
    } else {
        memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
        goto gm_fdone;
    }
    {
        {
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)0ULL));
            if (!t78) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&F.l10), (((uint8_t *)&F.l9) + 8), 200);
            {
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)0ULL));
                    if (!t80) {
                        goto gm_lb13;
                    }
                    {
                        memmove(((uint8_t *)&F.l11), (((uint8_t *)&F.l10) + 8), 48);
                    }
                    void * t81 = gm_l0;
                    void * t82 = (void *)(((uint8_t *)t81) + 8);
                    gm_t88rcucg1b7uk t83 = gm_l3;
                    uint8_t t84 = gm_f_is_integer_8d4f9ek(t82, GM_CONV(gm_csguankko4lmhd, t83));
                    uint8_t t85 = (uint8_t)!t84;
                    if (t85) {
                        gm_l17 = ((uint8_t)1);
                    } else {
                        void * t86 = gm_l0;
                        gm_t88rcucg1b7uk t87 = gm_l3;
                        uint8_t t88 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t86, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t87);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t89 = (uint8_t)!t88;
                        gm_l17 = t89;
                    }
                    uint8_t t90 = gm_l17;
                    if (t90) {
                        memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        void * t91 = gm_l0;
                        gm_t88rcucg1b7uk t92 = gm_l3;
                        gm_t428ue2u3hf5g t93;
                        gm_f_int_key_dmcraup((void *)&t93, (void *)NULL, cx, NULL, t91, t92, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t93);
                        ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb13:;
                {
                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)3ULL));
                    if (!t95) {
                        goto gm_lb14;
                    }
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 8));
                    gm_l12 = t96;
                    {
                        void * t97 = gm_l0;
                        void * t98 = (void *)(((uint8_t *)t97) + 8);
                        gm_t88rcucg1b7uk t99 = gm_l3;
                        gm_t53at80pfdus3 t100;
                        gm_f_kind_jcnt4hm((void *)&t100, t98, GM_CONV(gm_csguankko4lmhd, t99));
                        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t100) + 0));
                        uint8_t t102 = (uint8_t)(!(t101 == ((uint8_t)4ULL)));
                        gm_t53at80pfdus3 t103;
                        memset(&t103, 0, sizeof t103);
                        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)4ULL));
                        if (t102) {
                            memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_trqfvmjtt4sij t104;
                        gm_f_list_uvfu1k1((void *)&t104, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l13 = t104;
                    }
                    void * t105 = (void *)((uint8_t *)&F.l13);
                    uint8_t t106 = gm_l12;
                    if (t106) {
                        gm_l18 = ((uint8_t)1ULL);
                    } else {
                        gm_l18 = ((uint8_t)0ULL);
                    }
                    uint8_t t107 = gm_l18;
                    gm_tftb1odc739t5 t108 = gm_f_push_t83rg2m(t105, t107);
                    gm_l19 = t108;
                    uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcsffver18si54, 834, 13);
                    {
                        gm_t88rcucg1b7uk t110 = gm_l3;
                        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                        gm_tcmkompbl09il t113;
                        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t113 + 0), t110);
                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 8), t112);
                        gm_t428ue2u3hf5g t114;
                        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t114 + 8), t113);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t114);
                        ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb14:;
                {
                    memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
                    goto gm_fdone;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 826, 5);
            }
gm_lb12:;
        }
gm_lb11:;
        memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
        goto gm_fdone;
    }
gm_lb10:;
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
inline __attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_construction_call_ktqtrpo F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    void * gm_l6; /* inner */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l7;
    void * t12 = gm_l0;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)19ULL));
            if (!t14) {
                goto gm_lb2;
            }
            void * t15 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
            gm_l1 = t15;
            void * t16 = gm_l1;
            uint8_t t17 = gm_f_calls_on_value_q0pa6h5(t16);
            if (t17) {
                gm_l7 = ((void *)0);
            } else {
                void * t18 = gm_l0;
                gm_l7 = t18;
            }
            void * t19 = gm_l7;
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)28ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_t3mf58gv9kf3n t22 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t12) + 8) + 8));
            gm_l2 = t22;
            void * t23 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
            void * t24 = gm_f_construction_call_ktqtrpo(t23);
            F.ret = t24;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)24ULL));
            if (!t26) {
                goto gm_lb4;
            }
            void * t27 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
            gm_l3 = t27;
            void * t28 = gm_l3;
            void * t29 = gm_f_construction_call_ktqtrpo(t28);
            F.ret = t29;
            goto gm_fret;
        }
gm_lb4:;
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)25ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    void * t32 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t32;
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)26ULL));
                    if (!t34) {
                        goto gm_lb8;
                    }
                    void * t35 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t35;
                    goto gm_lb6;
                }
gm_lb8:;
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)27ULL));
                    if (!t37) {
                        goto gm_lb9;
                    }
                    void * t38 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
                    gm_l4 = t38;
                    goto gm_lb6;
                }
gm_lb9:;
                goto gm_lb5;
            }
gm_lb6:;
            void * t39 = gm_l4;
            void * t40 = gm_f_construction_call_ktqtrpo(t39);
            F.ret = t40;
            goto gm_fret;
        }
gm_lb5:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 258, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_decl_is_type_oh0cpn9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l2; /* depth */
    void * gm_l4; /* decl */
    GM_STACK_CHECK(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tagrkbqh054h7 gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    {
        {
            {
                void * t13 = gm_l0;
                uint64_t t14 = gm_l1;
                gm_f_state_a5bnsjb((void *)&gm_l6, (void *)NULL, cx, NULL, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l6) + 8), 208);
            }
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 144, 40);
            }
gm_lb3:;
            uint8_t t21 = gm_l7;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint32_t t22 = gm_l2;
    uint8_t t23 = (t22 > ((uint32_t)16ULL));
    if (t23) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 240));
    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 240) + 8));
    uint64_t t28 = gm_l1;
    if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcsffver18si54, 146, 14);
    void * t29 = (void *)(((uint8_t *)t26) + (uintptr_t)t28 * 216);
    gm_l4 = t29;
    {
        {
            void * t30 = gm_l4;
            uint8_t t31 = GM_LD(uint8_t, (((((uint8_t *)t30) + 40) + 64) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
            if (!t32) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l5), (((((uint8_t *)t30) + 40) + 64) + 8), 64);
            {
                {
                    uint8_t t33 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 24)) != 0);
                    if (t33) {
                        goto gm_lb10;
                    }
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 148, 8);
            }
gm_lb9:;
            uint8_t t34 = gm_l9;
            if (t34) {
                gm_l8 = ((uint8_t)1);
            } else {
                uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 8) + 8));
                uint8_t t36 = (!(t35 == ((uint64_t)1ULL)));
                gm_l8 = t36;
            }
            uint8_t t37 = gm_l8;
            if (t37) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t38 = gm_l0;
            void * t39 = gm_l4;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 32));
            void * t41 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint32_t t42 = gm_l2;
            uint8_t t43 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t38, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t43;
            goto gm_fret;
        }
gm_lb8:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_enum_array_type_0sic6t0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* index */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 896u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_t53at80pfdus3 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tdlpv0q69lov1 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    uint8_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 8);
    gm_l4 = t14;
    void * t15 = gm_l4;
    gm_t88rcucg1b7uk t16 = gm_l1;
    uint8_t t17 = gm_f_is_error_8k27gd1(t15, GM_CONV(gm_csguankko4lmhd, t16));
    if (t17) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t18 = gm_l4;
        gm_t88rcucg1b7uk t19 = gm_l2;
        uint8_t t20 = gm_f_is_error_8k27gd1(t18, GM_CONV(gm_csguankko4lmhd, t19));
        gm_l9 = t20;
    }
    uint8_t t21 = gm_l9;
    if (t21) {
        void * t22 = gm_l4;
        gm_t88rcucg1b7uk t23 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t23;
        goto gm_fret;
    }
    {
        void * t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l11, t24, GM_CONV(gm_csguankko4lmhd, t25));
    }
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)19ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 595, 8);
    }
gm_lb1:;
    uint8_t t28 = gm_l10;
    if (t28) {
        void * t29 = gm_l0;
        gm_tl874t2atnlh3 t30 = gm_l3;
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l1;
        gm_b16_8 t33 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
        gm_f_format_o2s8o9q((void *)&gm_l12, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 596, 23);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b0_1 t38 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t29, t30, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t39 = gm_l4;
        gm_t88rcucg1b7uk t40 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t40;
        goto gm_fret;
    }
    {
        void * t41 = gm_l0;
        gm_t88rcucg1b7uk t42 = gm_l1;
        gm_f_union_variants_a7khaqp((void *)&gm_l13, t41, GM_CONV(gm_csguankko4lmhd, t42));
    }
    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
    if (t43) {
        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l13), 24);
    } else {
        void * t44 = gm_l0;
        gm_tl874t2atnlh3 t45 = gm_l3;
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l1;
        gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t49;
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
        gm_f_format_qmamljh((void *)&gm_l14, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcsffver18si54, 600, 23);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t44, t45, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t54 = gm_l4;
        gm_t88rcucg1b7uk t55 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t55;
        goto gm_fret;
    }
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
    gm_l6 = t56;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t57 = gm_l7;
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t59 = (t57 < t58);
            if (!t59) {
                goto gm_lb4;
            }
            {
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t62 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t60) + (uintptr_t)t62 * 72), 72);
                        {
                            {
                                uint8_t t63 = (uint8_t)(((((uint8_t *)&F.l8) + 16))[4] != 0);
                                if (t63) {
                                    goto gm_lb10;
                                }
                                gm_l15 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l15 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 604, 12);
                        }
gm_lb9:;
                        uint8_t t64 = gm_l15;
                        if (t64) {
                            {
                                void * t65 = gm_l0;
                                gm_tl874t2atnlh3 t66 = gm_l3;
                                void * t67 = gm_l0;
                                gm_t88rcucg1b7uk t68 = gm_l1;
                                gm_b16_8 t69 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t67, t68);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                gm_t6j6smj7qoctu t71;
                                GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
                                GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
                                gm_f_format_il5kbp4((void *)&gm_l16, (void *)NULL, cx, NULL, &t71);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcsffver18si54, 605, 27);
                                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                                gm_b0_1 t75 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t74);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t76 = gm_l4;
                            gm_t88rcucg1b7uk t77 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t77;
                            goto gm_fret;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 603, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t78 = gm_l7;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            uint64_t t81 = r79;
            gm_l7 = t81;
        }
    }
gm_lb4:;
    {
        void * t82 = gm_l4;
        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t84 = (uint64_t)t83;
        gm_t88rcucg1b7uk t85 = gm_l2;
        gm_t88rcucg1b7uk t86 = gm_l1;
        gm_tnnjeerolos0a t87;
        GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87 + 4), t86);
        gm_t8nckeqfenl8a t88;
        GM_ST(uint32_t, (uint8_t *)&t88 + 20, 0);
        GM_ST(uint64_t, ((uint8_t *)&t88 + 0), t84);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t88 + 8), t85);
        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t88 + 12), t87);
        gm_t53at80pfdus3 t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t89 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t89 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t89 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)11ULL));
        GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t89 + 8), t88);
        gm_t88rcucg1b7uk t90 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t82, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t90;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_fn_ref_7alnaqh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* want */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint32_t gm_l15; /* g */
    void * gm_l16; /* info */
    GM_STACK_CHECK(&F, 1564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l18;
    gm_t53at80pfdus3 gm_l19;
    gm_t9805qtonbsag gm_l20;
    uint64_t gm_l21;
    gm_tf5srh4mp4b3i gm_l22;
    gm_t6qooom8qa2do gm_l23;
    gm_tf5srh4mp4b3i gm_l24;
    uint64_t gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    gm_t6qooom8qa2do gm_l27;
    gm_t88rcucg1b7uk t16 = gm_l3;
    gm_l4 = t16;
    {
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l18, t18, GM_CONV(gm_csguankko4lmhd, t19));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)13ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
            gm_l5 = t22;
            gm_t88rcucg1b7uk t23 = gm_l5;
            gm_l4 = t23;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t24 = gm_l0;
                void * t25 = (void *)(((uint8_t *)t24) + 8);
                gm_t88rcucg1b7uk t26 = gm_l4;
                gm_f_kind_jcnt4hm((void *)&gm_l19, t25, GM_CONV(gm_csguankko4lmhd, t26));
            }
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)16ULL));
            if (!t28) {
                goto gm_lb4;
            }
            {
                memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l19) + 8), 48);
            }
            gm_l7 = ((uint64_t)0ULL);
            gm_l8 = ((uint32_t)0ULL);
            void * t29 = gm_l2;
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)6ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t29) + 8) + 8), 32);
                    {
                        {
                            void * t32 = gm_l0;
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                            gm_t9805qtonbsag t34 = gm_f_local_fn_aji8qnd(t32, t33);
                            gm_l20 = t34;
                            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l20))[4] != 0);
                            if (!t35) {
                                goto gm_lb8;
                            }
                            uint32_t t36 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
                            gm_l10 = t36;
                            uint32_t t37 = gm_l10;
                            gm_l8 = t37;
                            goto gm_lb7;
                        }
gm_lb8:;
                        void * t38 = gm_l0;
                        uint64_t t39 = gm_l1;
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_tf5srh4mp4b3i t41 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t38, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l22 = t41;
                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l22))[8] != 0);
                        if (t42) {
                            uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                            gm_l21 = t43;
                        } else {
                            gm_t9805qtonbsag t44;
                            memset(&t44, 0, sizeof t44);
                            F.ret = t44;
                            goto gm_fret;
                        }
                        uint64_t t45 = gm_l21;
                        gm_l7 = t45;
                        {
                            {
                                void * t46 = gm_l0;
                                uint64_t t47 = gm_l7;
                                gm_f_resolve_decl_pus042v((void *)&gm_l23, (void *)NULL, cx, NULL, t46, t47);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)5ULL));
                                if (!t49) {
                                    goto gm_lb10;
                                }
                                uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 8));
                                gm_l11 = t50;
                                uint32_t t51 = gm_l11;
                                gm_l8 = t51;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_t9805qtonbsag t52;
                            memset(&t52, 0, sizeof t52);
                            F.ret = t52;
                            goto gm_fret;
                        }
gm_lb9:;
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)22ULL));
                    if (!t54) {
                        goto gm_lb11;
                    }
                    void * t55 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
                    gm_l12 = t55;
                    memmove(((uint8_t *)&gm_l13), (((((uint8_t *)t29) + 8) + 8) + 8), 32);
                    void * t56 = gm_l0;
                    uint64_t t57 = gm_l1;
                    void * t58 = gm_l12;
                    gm_tf5srh4mp4b3i t59 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t56, t57, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t59;
                    uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l24))[8] != 0);
                    if (t60) {
                        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                        gm_l14 = t61;
                    } else {
                        gm_t9805qtonbsag t62;
                        memset(&t62, 0, sizeof t62);
                        F.ret = t62;
                        goto gm_fret;
                    }
                    void * t63 = gm_l0;
                    void * t64 = GM_LD(void *, ((uint8_t *)t63));
                    uint64_t t65 = gm_l14;
                    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                    gm_tf5srh4mp4b3i t67 = gm_f_lookup_59juqoo(t64, t65, t66);
                    gm_l26 = t67;
                    uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
                    if (t68) {
                        uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                        gm_l25 = t69;
                    } else {
                        gm_t9805qtonbsag t70;
                        memset(&t70, 0, sizeof t70);
                        F.ret = t70;
                        goto gm_fret;
                    }
                    uint64_t t71 = gm_l25;
                    gm_l7 = t71;
                    void * t72 = gm_l0;
                    uint64_t t73 = gm_l7;
                    uint64_t t74 = gm_l1;
                    uint8_t t75 = gm_f_decl_visible_bd168ti(t72, t73, t74);
                    uint8_t t76 = (uint8_t)!t75;
                    if (t76) {
                        gm_t9805qtonbsag t77;
                        memset(&t77, 0, sizeof t77);
                        F.ret = t77;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t78 = gm_l0;
                            uint64_t t79 = gm_l7;
                            gm_f_resolve_decl_pus042v((void *)&gm_l27, (void *)NULL, cx, NULL, t78, t79);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)5ULL));
                            if (!t81) {
                                goto gm_lb13;
                            }
                            uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&gm_l27) + 8));
                            gm_l15 = t82;
                            uint32_t t83 = gm_l15;
                            gm_l8 = t83;
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_t9805qtonbsag t84;
                        memset(&t84, 0, sizeof t84);
                        F.ret = t84;
                        goto gm_fret;
                    }
gm_lb12:;
                    goto gm_lb5;
                }
gm_lb11:;
                {
                    gm_t9805qtonbsag t85;
                    memset(&t85, 0, sizeof t85);
                    F.ret = t85;
                    goto gm_fret;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 849, 5);
            }
gm_lb5:;
            void * t86 = gm_l0;
            void * t87 = GM_LD(void *, (((uint8_t *)t86) + 344));
            uint64_t t88 = GM_LD(uint64_t, ((((uint8_t *)t86) + 344) + 8));
            uint32_t t89 = gm_l8;
            uint64_t t90 = (uint64_t)t89;
            if ((uintptr_t)t90 >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)t90, t88, gm_srcsffver18si54, 868, 14);
            void * t91 = (void *)(((uint8_t *)t87) + (uintptr_t)t90 * 136);
            gm_l16 = t91;
            void * t92 = gm_l16;
            uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t92) + 80) + 8));
            uint8_t t94 = (t93 > ((uint64_t)0ULL));
            if (t94) {
                gm_t9805qtonbsag t95;
                memset(&t95, 0, sizeof t95);
                F.ret = t95;
                goto gm_fret;
            }
            {
                void * t96 = gm_l0;
                void * t97 = gm_l16;
                gm_f_fn_ty_qaih7ts((void *)&F.l17, (void *)NULL, cx, NULL, t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t98 = gm_l0;
                void * t99 = (void *)(((uint8_t *)t98) + 8);
                gm_tddtjfggdat0d t100 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l17));
                gm_t53at80pfdus3 t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)16ULL));
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t101 + 8), t100);
                gm_t88rcucg1b7uk t102 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t99, &t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t103 = gm_l0;
                void * t104 = (void *)(((uint8_t *)t103) + 8);
                gm_tddtjfggdat0d t105 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l6));
                gm_t53at80pfdus3 t106;
                GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)16ULL));
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t106 + 8), t105);
                gm_t88rcucg1b7uk t107 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t104, &t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t108 = (!gm_eq9m6mvtmgde(((uint8_t *)&t102), ((uint8_t *)&t107)));
                if (t108) {
                    gm_t9805qtonbsag t109;
                    memset(&t109, 0, sizeof t109);
                    F.ret = t109;
                    goto gm_fret;
                }
            }
            uint32_t t110 = gm_l8;
            gm_t9805qtonbsag t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t111), t110);
            ((uint8_t *)&t111)[4] = 1;
            F.ret = t111;
            goto gm_fret;
        }
gm_lb4:;
        gm_t9805qtonbsag t112;
        memset(&t112, 0, sizeof t112);
        F.ret = t112;
        goto gm_fret;
    }
gm_lb3:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_ty_qaih7ts F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 255u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_nof7vfj((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t7;
    }
    void * t8 = gm_l1;
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 40));
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
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t13) + (uintptr_t)t15 * 56), 56);
                        void * t16 = (void *)((uint8_t *)&F.l2);
                        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t18 = gm_f_push_k3l9nic(t16, GM_CONV(gm_csguankko4lmhd, t17));
                        gm_l6 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcsffver18si54, 530, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 530, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l4;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l4 = t23;
        }
    }
gm_lb1:;
    {
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        void * t26 = gm_l1;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 56));
        void * t28 = gm_l1;
        gm_tnpifv20irnph t29 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t28) + 64));
        void * t30 = gm_l1;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t30) + 128));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 44, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0), t25);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16), t27);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24), t29);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40), t31);
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
inline __attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3) {
    struct gm_fr_generic_decl_oir9hm6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    uint8_t gm_l3; /* report */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l9; /* d */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 616u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l11;
    gm_tf5srh4mp4b3i gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    void * t6 = gm_l2;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)6ULL));
            if (!t8) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t6) + 8) + 8), 32);
            }
            void * t9 = gm_l0;
            uint64_t t10 = gm_l1;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_tf5srh4mp4b3i t12 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
            if (t13) {
                uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l5 = t14;
            } else {
                memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
                goto gm_fdone;
            }
            {
                uint64_t t15 = gm_l5;
                gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_tkb8hpcqf70t1 t17;
                GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
                GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t15);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t17 + 8), t16);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
                GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out)), t17);
                ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)22ULL));
            if (!t19) {
                goto gm_lb3;
            }
            void * t20 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l6 = t20;
            {
                memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t6) + 8) + 8) + 8), 32);
            }
            void * t21 = gm_l0;
            uint64_t t22 = gm_l1;
            void * t23 = gm_l6;
            gm_tf5srh4mp4b3i t24 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t24;
            uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
            if (t25) {
                uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                gm_l8 = t26;
            } else {
                memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
                goto gm_fdone;
            }
            uint8_t t27 = gm_l3;
            if (t27) {
                {
                    void * t28 = gm_l0;
                    uint64_t t29 = gm_l8;
                    uint64_t t30 = gm_l1;
                    gm_tf5srh4mp4b3i t31 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t28, t29, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t31;
                }
                uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                if (t32) {
                    uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                    gm_l9 = t33;
                } else {
                    memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
                    goto gm_fdone;
                }
                {
                    uint64_t t34 = gm_l9;
                    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                    gm_tkb8hpcqf70t1 t36;
                    GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t34);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t36 + 8), t35);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
                    GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out)), t36);
                    ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
                    goto gm_fdone;
                }
            }
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            uint64_t t39 = gm_l8;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tf5srh4mp4b3i t41 = gm_f_lookup_59juqoo(t38, t39, t40);
            gm_l14 = t41;
            uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
            if (t42) {
                uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                gm_l10 = t43;
            } else {
                memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
                goto gm_fdone;
            }
            void * t44 = gm_l0;
            uint64_t t45 = gm_l10;
            uint64_t t46 = gm_l1;
            uint8_t t47 = gm_f_decl_visible_bd168ti(t44, t45, t46);
            uint8_t t48 = (uint8_t)!t47;
            if (t48) {
                memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
                goto gm_fdone;
            }
            void * t49 = gm_l0;
            uint64_t t50 = gm_l10;
            uint64_t t51 = gm_l1;
            gm_b0_1 t52 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                uint64_t t53 = gm_l10;
                gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                gm_tkb8hpcqf70t1 t55;
                GM_ST(uint32_t, (uint8_t *)&t55 + 20, 0);
                GM_ST(uint64_t, ((uint8_t *)&t55 + 0), t53);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t55 + 8), t54);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
                GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out)), t55);
                ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            memset(&(*(gm_toblvhepnjqkh *)gm_out), 0, sizeof (*(gm_toblvhepnjqkh *)gm_out));
            goto gm_fdone;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 554, 5);
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
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_5b8bvov F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* template */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* next */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t08olq97rscn3 gm_l18; /* n */
    gm_tf5srh4mp4b3i gm_l20; /* found */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* j */
    uint32_t gm_l24; /* p */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint32_t gm_l29; /* pid */
    uint64_t gm_l31; /* m */
    void * gm_l33; /* d */
    void * gm_l35; /* e */
    gm_t88rcucg1b7uk gm_l36; /* vt */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* j */
    uint32_t gm_l41; /* q */
    gm_t88rcucg1b7uk gm_l42; /* t */
    GM_STACK_CHECK(&F, 3006u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l43;
    gm_tn68el8d14vku gm_l44;
    uint64_t gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tabb710d9mq2e gm_l47;
    uint8_t gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tn68el8d14vku gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tn68el8d14vku gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_t88rcucg1b7uk gm_l55;
    gm_tkn42u7na3t81 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    {
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 1088));
        uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 1088) + 8));
        uint32_t t45 = gm_l2;
        uint64_t t46 = (uint64_t)t45;
        if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_srcsffver18si54, 707, 13);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t43) + (uintptr_t)t46 * 184), 184);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
        gm_l6 = t47;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_l7 = t48;
        gm_trqfvmjtt4sij t49;
        gm_f_list_9tqbks7((void *)&t49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t49;
        gm_b16_8 t50 = gm_l6;
        gm_l9 = t50;
        gm_l10 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t51 = gm_l10;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t53 = (t51 < t52);
                if (!t53) {
                    goto gm_lb1;
                }
                {
                    void * t54 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint64_t t56 = gm_l10;
                    {
                        {
                            void * t57 = (void *)((uint8_t *)&F.l8);
                            gm_t0mfir12leg2g t58;
                            memset(&t58, 0, sizeof t58);
                            gm_tftb1odc739t5 t59 = gm_f_push_k0n7eot(t57, &t58);
                            gm_l43 = t59;
                            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                            if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcsffver18si54, 712, 23);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 712, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t61 = gm_l10;
                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                uint64_t t64 = r62;
                gm_l10 = t64;
            }
        }
gm_lb1:;
        gm_l11 = ((uint64_t)0ULL);
        gm_b16_8 t65 = gm_l3;
        gm_l12 = t65;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t66 = gm_l13;
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t68 = (t66 < t67);
                if (!t68) {
                    goto gm_lb6;
                }
                {
                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint64_t t71 = gm_l13;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l14), (((uint8_t *)t69) + (uintptr_t)t71 * 144), 144);
                            }
                            gm_l15 = ((uint64_t)0ULL);
                            {
                                memset(&F.l16, 0, sizeof F.l16);
                            }
                            {
                                {
                                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)0ULL));
                                    if (!t73) {
                                        goto gm_lb12;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l14) + 8), 88);
                                    }
                                    uint64_t t74 = gm_l11;
                                    gm_l15 = t74;
                                    uint64_t t75 = gm_l11;
                                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                                    if (o77) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 720, 17);
                                    uint64_t t78 = r76;
                                    gm_l11 = t78;
                                    {
                                        gm_tc3oht4ksth9d t79 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l17));
                                        GM_ST(uint8_t, (uint8_t *)&F.l16 + 89, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.l16 + 90, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.l16 + 92, 0);
                                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l16), t79);
                                        ((uint8_t *)&F.l16)[88] = 1;
                                    }
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)1ULL));
                                    if (!t81) {
                                        goto gm_lb13;
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l18), (((uint8_t *)&F.l14) + 8), 32);
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l19), ((((uint8_t *)&F.l14) + 8) + 32), 88);
                                    }
                                    gm_tf5srh4mp4b3i t82;
                                    memset(&t82, 0, sizeof t82);
                                    gm_l20 = t82;
                                    gm_b16_8 t83 = gm_l6;
                                    gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                                    gm_l21 = t84;
                                    gm_l22 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t85 = gm_l22;
                                            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                            uint8_t t87 = (t85 < t86);
                                            if (!t87) {
                                                goto gm_lb14;
                                            }
                                            {
                                                uint64_t t88 = gm_l22;
                                                void * t89 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                                uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                uint64_t t91 = gm_l22;
                                                uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)t89) + (uintptr_t)t91 * 4));
                                                gm_tn68el8d14vku t93;
                                                GM_ST(uint32_t, (uint8_t *)&t93 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t93 + 0), t88);
                                                GM_ST(uint32_t, ((uint8_t *)&t93 + 8), t92);
                                                gm_l44 = t93;
                                                {
                                                    {
                                                        uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                                                        gm_l23 = t94;
                                                        uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                                                        gm_l24 = t95;
                                                        void * t96 = gm_l0;
                                                        void * t97 = (void *)(((uint8_t *)t96) + 8);
                                                        uint32_t t98 = gm_l24;
                                                        gm_b16_8 t99 = gm_f_param_name_rbnop5l(t97, t98);
                                                        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                        uint8_t t101 = (gm_eq0iqiubfm7e(((uint8_t *)&t99), ((uint8_t *)&t100)));
                                                        if (t101) {
                                                            uint64_t t102 = gm_l23;
                                                            gm_tf5srh4mp4b3i t103;
                                                            GM_ST(uint8_t, (uint8_t *)&t103 + 9, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t103 + 10, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t103 + 12, 0);
                                                            GM_ST(uint64_t, ((uint8_t *)&t103), t102);
                                                            ((uint8_t *)&t103)[8] = 1;
                                                            gm_l20 = t103;
                                                            goto gm_lb14;
                                                        }
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 725, 17);
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                            uint64_t t104 = gm_l22;
                                            uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                                            uint64_t t107 = r105;
                                            gm_l22 = t107;
                                        }
                                    }
gm_lb14:;
                                    uint8_t t108 = (uint8_t)((((uint8_t *)&gm_l20))[8] != 0);
                                    if (t108) {
                                        uint64_t t109 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                        gm_l45 = t109;
                                    } else {
                                        {
                                            void * t110 = gm_l0;
                                            gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 16));
                                            gm_b16_8 t112 = gm_l7;
                                            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                            gm_t6j6smj7qoctu t114;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t112);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t113);
                                            gm_f_format_bnsf58k((void *)&gm_l46, (void *)NULL, cx, NULL, &t114);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                            if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcsffver18si54, 732, 37);
                                            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                                            gm_b0_1 t118 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t110, t111, t117);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        {
                                            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                            goto gm_fdone;
                                        }
                                    }
                                    uint64_t t119 = gm_l45;
                                    gm_l15 = t119;
                                    {
                                        gm_tc3oht4ksth9d t120 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l19));
                                        GM_ST(uint8_t, (uint8_t *)&F.l16 + 89, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.l16 + 90, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.l16 + 92, 0);
                                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l16), t120);
                                        ((uint8_t *)&F.l16)[88] = 1;
                                    }
                                    goto gm_lb11;
                                }
gm_lb13:;
                                {
                                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)2ULL));
                                    if (!t122) {
                                        goto gm_lb19;
                                    }
                                    void * t123 = gm_l0;
                                    gm_tl874t2atnlh3 t124 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                                    gm_b16_8 t125;
                                    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sldikqa9i70sm));
                                    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(51));
                                    gm_b0_1 t126 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t123, t124, t125);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                    goto gm_fdone;
                                }
gm_lb19:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 717, 9);
                            }
gm_lb11:;
                            uint64_t t127 = gm_l15;
                            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t129 = (t127 >= t128);
                            if (t129) {
                                {
                                    void * t130 = gm_l0;
                                    gm_tl874t2atnlh3 t131 = gm_l4;
                                    gm_b16_8 t132 = gm_l7;
                                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    gm_t3877pk7lv748 t134;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t132);
                                    GM_ST(uint64_t, ((uint8_t *)&t134 + 16), t133);
                                    gm_f_format_n6tblha((void *)&gm_l47, (void *)NULL, cx, NULL, &t134);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srcsffver18si54, 743, 27);
                                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                                    gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                                    gm_b0_1 t138 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t130, t131, t137);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                    goto gm_fdone;
                                }
                            }
                            void * t139 = GM_LD(void *, ((uint8_t *)&F.l8));
                            uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            uint64_t t141 = gm_l15;
                            if ((uintptr_t)t141 >= (uintptr_t)(t140)) gm_trap_index((uintptr_t)t141, t140, gm_srcsffver18si54, 746, 12);
                            {
                                {
                                    uint8_t t142 = (uint8_t)(((((uint8_t *)t139) + (uintptr_t)t141 * 96))[88] != 0);
                                    if (t142) {
                                        goto gm_lb21;
                                    }
                                    gm_l48 = ((uint8_t)0);
                                    goto gm_lb20;
                                }
gm_lb21:;
                                {
                                    gm_l48 = ((uint8_t)1);
                                    goto gm_lb20;
                                }
gm_lb22:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 746, 12);
                            }
gm_lb20:;
                            uint8_t t143 = gm_l48;
                            if (t143) {
                                {
                                    void * t144 = gm_l0;
                                    gm_tl874t2atnlh3 t145 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                                    void * t146 = gm_l0;
                                    void * t147 = (void *)(((uint8_t *)t146) + 8);
                                    void * t148 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t150 = gm_l15;
                                    uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)t148) + (uintptr_t)t150 * 4));
                                    gm_b16_8 t152 = gm_f_param_name_rbnop5l(t147, t151);
                                    gm_tnpifv20irnph t153;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t153 + 0), t152);
                                    gm_f_format_nbjjmv4((void *)&gm_l49, (void *)NULL, cx, NULL, t153);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                    if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_srcsffver18si54, 747, 29);
                                    gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                                    gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                                    gm_b0_1 t157 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t144, t145, t156);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                    goto gm_fdone;
                                }
                            }
                            {
                                void * t158 = GM_LD(void *, ((uint8_t *)&F.l8));
                                uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                uint64_t t160 = gm_l15;
                                if ((uintptr_t)t160 >= (uintptr_t)(t159)) gm_trap_index((uintptr_t)t160, t159, gm_srcsffver18si54, 750, 9);
                                memmove((((uint8_t *)t158) + (uintptr_t)t160 * 96), ((uint8_t *)&F.l16), 96);
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 714, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t161 = gm_l13;
                uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                uint64_t t164 = r162;
                gm_l13 = t164;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t165;
        gm_f_list_nof7vfj((void *)&t165, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l25 = t165;
        gm_b16_8 t166 = gm_l6;
        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
        gm_l26 = t167;
        gm_l27 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t168 = gm_l27;
                uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                uint8_t t170 = (t168 < t169);
                if (!t170) {
                    goto gm_lb23;
                }
                {
                    uint64_t t171 = gm_l27;
                    void * t172 = GM_LD(void *, ((uint8_t *)&gm_l26));
                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t174 = gm_l27;
                    uint32_t t175 = GM_LD(uint32_t, (((uint8_t *)t172) + (uintptr_t)t174 * 4));
                    gm_tn68el8d14vku t176;
                    GM_ST(uint32_t, (uint8_t *)&t176 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t176 + 0), t171);
                    GM_ST(uint32_t, ((uint8_t *)&t176 + 8), t175);
                    gm_l50 = t176;
                    {
                        {
                            uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)&gm_l50));
                            gm_l28 = t177;
                            uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 8));
                            gm_l29 = t178;
                            memset(&F.l30, 0, sizeof F.l30);
                            uint64_t t179 = gm_l1;
                            gm_l31 = t179;
                            {
                                {
                                    {
                                        void * t180 = GM_LD(void *, ((uint8_t *)&F.l8));
                                        uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                        uint64_t t182 = gm_l28;
                                        if ((uintptr_t)t182 >= (uintptr_t)(t181)) gm_trap_index((uintptr_t)t182, t181, gm_srcsffver18si54, 756, 17);
                                        uint8_t t183 = (uint8_t)(((((uint8_t *)t180) + (uintptr_t)t182 * 96))[88] != 0);
                                        if (!t183) {
                                            goto gm_lb29;
                                        }
                                        memmove(((uint8_t *)&F.l32), (((uint8_t *)t180) + (uintptr_t)t182 * 96), 88);
                                    }
                                    {
                                        gm_tc3oht4ksth9d t184 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l32));
                                        GM_ST(uint8_t, (uint8_t *)&F.l30 + 89, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.l30 + 90, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.l30 + 92, 0);
                                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l30), t184);
                                        ((uint8_t *)&F.l30)[88] = 1;
                                    }
                                    goto gm_lb28;
                                }
gm_lb29:;
                                {
                                    {
                                        void * t185 = GM_LD(void *, (((uint8_t *)&F.l5) + 136));
                                        uint64_t t186 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 136) + 8));
                                        uint64_t t187 = gm_l28;
                                        if ((uintptr_t)t187 >= (uintptr_t)(t186)) gm_trap_index((uintptr_t)t187, t186, gm_srcsffver18si54, 758, 24);
                                        uint8_t t188 = (uint8_t)(GM_LD(void *, (((uint8_t *)t185) + (uintptr_t)t187 * 8)) != 0);
                                        if (!t188) {
                                            goto gm_lb31;
                                        }
                                        void * t189 = GM_LD(void *, (((uint8_t *)t185) + (uintptr_t)t187 * 8));
                                        gm_l33 = t189;
                                        {
                                            void * t190 = gm_l33;
                                            gm_tc3oht4ksth9d t191 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t190));
                                            GM_ST(uint8_t, (uint8_t *)&F.l30 + 89, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l30 + 90, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l30 + 92, 0);
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l30), t191);
                                            ((uint8_t *)&F.l30)[88] = 1;
                                        }
                                        uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
                                        gm_l31 = t192;
                                        goto gm_lb30;
                                    }
gm_lb31:;
                                    void * t193 = gm_l0;
                                    void * t194 = GM_LD(void *, (((uint8_t *)t193) + 1128));
                                    uint64_t t195 = GM_LD(uint64_t, ((((uint8_t *)t193) + 1128) + 8));
                                    uint32_t t196 = gm_l29;
                                    uint64_t t197 = (uint64_t)t196;
                                    if ((uintptr_t)t197 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t197, t195, gm_srcsffver18si54, 762, 19);
                                    gm_tnnjeerolos0a t198 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t194) + (uintptr_t)t197 * 48));
                                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&t198) + 0));
                                    uint8_t t200 = (uint8_t)((t199 == ((uint8_t)1ULL)));
                                    gm_tnnjeerolos0a t201;
                                    GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)1ULL));
                                    if (t200) {
                                        void * t202 = (void *)((uint8_t *)&F.l25);
                                        void * t203 = gm_l0;
                                        void * t204 = (void *)(((uint8_t *)t203) + 8);
                                        gm_tnpifv20irnph t205 = gm_f_empty_effects_r0iv3pv();
                                        gm_t53at80pfdus3 t206;
                                        memset(&t206, 0, sizeof t206);
                                        GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)24ULL));
                                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t206 + 8), t205);
                                        gm_t88rcucg1b7uk t207 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t204, &t206);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t208 = gm_f_push_k3l9nic(t202, GM_CONV(gm_csguankko4lmhd, t207));
                                        gm_l51 = t208;
                                        uint8_t t209 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                        if (!t209) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcsffver18si54, 764, 13);
                                        goto gm_lb25;
                                    } else {
                                        {
                                            void * t210 = gm_l0;
                                            gm_tl874t2atnlh3 t211 = gm_l4;
                                            void * t212 = gm_l0;
                                            void * t213 = (void *)(((uint8_t *)t212) + 8);
                                            uint32_t t214 = gm_l29;
                                            gm_b16_8 t215 = gm_f_param_name_rbnop5l(t213, t214);
                                            gm_b16_8 t216 = gm_l7;
                                            gm_t6j6smj7qoctu t217;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t217 + 0), t215);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t217 + 16), t216);
                                            gm_f_format_l0aimgq((void *)&gm_l52, (void *)NULL, cx, NULL, &t217);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                            if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcsffver18si54, 767, 27);
                                            gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                                            gm_b0_1 t221 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t210, t211, t220);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        {
                                            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                            goto gm_fdone;
                                        }
                                    }
                                }
gm_lb30:;
                            }
gm_lb28:;
                            uint8_t t222 = (uint8_t)((((uint8_t *)&F.l30))[88] != 0);
                            if (t222) {
                                memmove(((uint8_t *)&F.l34), ((uint8_t *)&F.l30), 88);
                            } else {
                                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                goto gm_fdone;
                            }
                            void * t223 = (void *)((uint8_t *)&F.l34);
                            gm_l35 = t223;
                            void * t224 = gm_l0;
                            void * t225 = GM_LD(void *, (((uint8_t *)t224) + 1128));
                            uint64_t t226 = GM_LD(uint64_t, ((((uint8_t *)t224) + 1128) + 8));
                            uint32_t t227 = gm_l29;
                            uint64_t t228 = (uint64_t)t227;
                            if ((uintptr_t)t228 >= (uintptr_t)(t226)) gm_trap_index((uintptr_t)t228, t226, gm_srcsffver18si54, 772, 20);
                            {
                                {
                                    uint8_t t229 = GM_LD(uint8_t, ((((uint8_t *)t225) + (uintptr_t)t228 * 48) + 0));
                                    uint8_t t230 = (uint8_t)(t229 == ((uint8_t)2ULL));
                                    if (!t230) {
                                        goto gm_lb33;
                                    }
                                    gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t225) + (uintptr_t)t228 * 48) + 4));
                                    gm_l36 = t231;
                                    gm_trqfvmjtt4sij t232;
                                    gm_f_list_oalq03d((void *)&t232, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l37 = t232;
                                    void * t233 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                    uint64_t t236 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t237 = (uintptr_t)t235;
                                    if (t236 > t237 || t237 > (uintptr_t)t234) gm_trap_slice(t236, t237, t234, gm_srcsffver18si54, 775, 39);
                                    uint64_t t238 = ((uintptr_t)t235) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t239;
                                    GM_ST(void *, (uint8_t *)&t239, (void *)(((uint8_t *)t233 + (uintptr_t)((uint64_t)0ULL) * 4)));
                                    GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(t238));
                                    gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                                    gm_l38 = t240;
                                    gm_l39 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t241 = gm_l39;
                                            uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint8_t t243 = (t241 < t242);
                                            if (!t243) {
                                                goto gm_lb34;
                                            }
                                            {
                                                uint64_t t244 = gm_l39;
                                                void * t245 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                uint64_t t247 = gm_l39;
                                                uint32_t t248 = GM_LD(uint32_t, (((uint8_t *)t245) + (uintptr_t)t247 * 4));
                                                gm_tn68el8d14vku t249;
                                                GM_ST(uint32_t, (uint8_t *)&t249 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t249 + 0), t244);
                                                GM_ST(uint32_t, ((uint8_t *)&t249 + 8), t248);
                                                gm_l53 = t249;
                                                {
                                                    {
                                                        uint64_t t250 = GM_LD(uint64_t, ((uint8_t *)&gm_l53));
                                                        gm_l40 = t250;
                                                        uint32_t t251 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 8));
                                                        gm_l41 = t251;
                                                        void * t252 = (void *)((uint8_t *)&F.l37);
                                                        uint32_t t253 = gm_l41;
                                                        void * t254 = GM_LD(void *, ((uint8_t *)&F.l25));
                                                        uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                                        uint64_t t256 = gm_l40;
                                                        if ((uintptr_t)t256 >= (uintptr_t)(t255)) gm_trap_index((uintptr_t)t256, t255, gm_srcsffver18si54, 775, 96);
                                                        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t254) + (uintptr_t)t256 * 4));
                                                        gm_tplpbqf3duvn3 t258;
                                                        GM_ST(uint32_t, ((uint8_t *)&t258 + 0), t253);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t258 + 4), t257);
                                                        gm_tftb1odc739t5 t259 = gm_f_push_h69n5fi(t252, GM_CONV(gm_cs7e9ppsd384bj, t258));
                                                        gm_l54 = t259;
                                                        uint8_t t260 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                        if (!t260) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcsffver18si54, 775, 67);
                                                        goto gm_lb37;
                                                    }
gm_lb38:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 775, 17);
                                                }
gm_lb37:;
                                            }
gm_lb36:;
                                            uint64_t t261 = gm_l39;
                                            uint64_t r262; bool o263 = __builtin_add_overflow(t261, ((uint64_t)1ULL), &r262);
                                            uint64_t t264 = r262;
                                            gm_l39 = t264;
                                        }
                                    }
gm_lb34:;
                                    void * t265 = gm_l0;
                                    uint64_t t266 = gm_l31;
                                    void * t267 = gm_l0;
                                    gm_t88rcucg1b7uk t268 = gm_l36;
                                    gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                                    gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                                    gm_t88rcucg1b7uk t271 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t267, t268, t270);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t272 = gm_l35;
                                    gm_tkn42u7na3t81 t273 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t265, t266, t271, t272);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l56 = t273;
                                    uint8_t t274 = (uint8_t)((((uint8_t *)&gm_l56))[4] != 0);
                                    if (t274) {
                                        gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l56));
                                        gm_l55 = t275;
                                    } else {
                                        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                        goto gm_fdone;
                                    }
                                    gm_t88rcucg1b7uk t276 = gm_l55;
                                    gm_l42 = t276;
                                    goto gm_lb32;
                                }
gm_lb33:;
                                {
                                    uint8_t t277 = GM_LD(uint8_t, ((((uint8_t *)t225) + (uintptr_t)t228 * 48) + 0));
                                    uint8_t t278 = (uint8_t)(t277 == ((uint8_t)1ULL));
                                    if (!t278) {
                                        goto gm_lb39;
                                    }
                                    void * t279 = gm_l0;
                                    uint64_t t280 = gm_l31;
                                    void * t281 = gm_l35;
                                    gm_t88rcucg1b7uk t282 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t279, t280, t281);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l42 = t282;
                                    goto gm_lb32;
                                }
gm_lb39:;
                                {
                                    uint8_t t283 = GM_LD(uint8_t, ((((uint8_t *)t225) + (uintptr_t)t228 * 48) + 0));
                                    uint8_t t284 = (uint8_t)(t283 == ((uint8_t)0ULL));
                                    if (!t284) {
                                        goto gm_lb40;
                                    }
                                    void * t285 = gm_l0;
                                    uint64_t t286 = gm_l31;
                                    void * t287 = gm_l35;
                                    gm_t88rcucg1b7uk t288 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t285, t286, t287);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l42 = t288;
                                    goto gm_lb32;
                                }
gm_lb40:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 772, 14);
                            }
gm_lb32:;
                            gm_t88rcucg1b7uk t289 = gm_l42;
                            gm_l42 = t289;
                            void * t290 = gm_l0;
                            void * t291 = (void *)(((uint8_t *)t290) + 8);
                            gm_t88rcucg1b7uk t292 = gm_l42;
                            uint8_t t293 = gm_f_is_error_8k27gd1(t291, GM_CONV(gm_csguankko4lmhd, t292));
                            if (t293) {
                                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                goto gm_fdone;
                            }
                            void * t294 = (void *)((uint8_t *)&F.l25);
                            gm_t88rcucg1b7uk t295 = gm_l42;
                            gm_tftb1odc739t5 t296 = gm_f_push_k3l9nic(t294, GM_CONV(gm_csguankko4lmhd, t295));
                            gm_l57 = t296;
                            uint8_t t297 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                            if (!t297) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcsffver18si54, 782, 9);
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 753, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t298 = gm_l27;
                uint64_t r299; bool o300 = __builtin_add_overflow(t298, ((uint64_t)1ULL), &r299);
                uint64_t t301 = r299;
                gm_l27 = t301;
            }
        }
gm_lb23:;
    }
    {
        gm_b16_8 t302 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t302);
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
extern __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hoists_13k2a2s F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* x */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    void * gm_l5;
    void * t9 = gm_l0;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)28ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t9) + 8) + 8));
            gm_l1 = t12;
            void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)24ULL));
            if (!t15) {
                goto gm_lb3;
            }
            void * t16 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t16;
            void * t17 = gm_l2;
            gm_l3 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 285, 14);
    }
gm_lb1:;
    void * t18 = gm_l3;
    void * t19 = gm_f_construction_call_ktqtrpo(t18);
    gm_l5 = t19;
    {
        {
            uint8_t t20 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
            if (t20) {
                goto gm_lb6;
            }
            gm_l4 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l4 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 290, 8);
    }
gm_lb5:;
    uint8_t t21 = gm_l4;
    if (t21) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t22 = gm_l0;
    void * t23 = gm_f_prefix_operand_3n5jchh(t22);
    uint8_t t24 = gm_f_pushes_down_0djaflp(t23);
    F.ret = t24;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_type_name_a77a0kf F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 334u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = (t7 < ((uint64_t)2ULL));
    if (t8) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_srcsffver18si54, 5, 24);
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 1), ((uint8_t)105ULL));
        uint8_t t13 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 0, ((uint8_t *)&t11))) t13 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 1, ((uint8_t *)&t11))) t13 = 1;
        uint8_t t14 = (uint8_t)(!t13);
        gm_l9 = t14;
    }
    uint8_t t15 = gm_l9;
    if (t15) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t17, gm_srcsffver18si54, 5, 53);
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t19 = ((t18 == ((uint8_t)48ULL)));
        gm_l8 = t19;
    }
    uint8_t t20 = gm_l8;
    if (t20) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t22 = (t21 > ((uint64_t)4ULL));
        gm_l7 = t22;
    }
    uint8_t t23 = gm_l7;
    if (t23) {
        gm_tp8mv052i18rg t24;
        memset(&t24, 0, sizeof t24);
        F.ret = t24;
        goto gm_fret;
    }
    gm_l1 = ((uint32_t)0ULL);
    void * t25 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t27 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t28 = (uintptr_t)t26;
    if (t27 > t28 || t28 > (uintptr_t)t26) gm_trap_slice(t27, t28, t26, gm_srcsffver18si54, 7, 14);
    uint64_t t29 = ((uintptr_t)t26) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
    gm_l2 = t30;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l3;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t36 = gm_l3;
                {
                    {
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t36 * 1));
                        gm_l4 = t37;
                        uint8_t t38 = gm_l4;
                        gm_l5 = t38;
                        uint8_t t39 = gm_l5;
                        uint8_t t40 = (((uint8_t)48ULL) <= t39);
                        if (t40) {
                            uint8_t t41 = gm_l5;
                            uint8_t t42 = (t41 <= ((uint8_t)57ULL));
                            gm_l10 = t42;
                        } else {
                            gm_l10 = ((uint8_t)0);
                        }
                        uint8_t t43 = gm_l10;
                        uint8_t t44 = (uint8_t)!t43;
                        if (t44) {
                            gm_tp8mv052i18rg t45;
                            memset(&t45, 0, sizeof t45);
                            F.ret = t45;
                            goto gm_fret;
                        }
                        uint32_t t46 = gm_l1;
                        uint32_t r47; bool o48 = __builtin_mul_overflow(t46, ((uint32_t)10ULL), &r47);
                        if (o48) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 9, 16);
                        uint32_t t49 = r47;
                        uint8_t t50 = gm_l4;
                        uint8_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint8_t)48ULL), &r51);
                        if (o52) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 9, 32);
                        uint8_t t53 = r51;
                        uint32_t t54 = (uint32_t)t53;
                        uint32_t r55; bool o56 = __builtin_add_overflow(t49, t54, &r55);
                        if (o56) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 9, 16);
                        uint32_t t57 = r55;
                        gm_l1 = t57;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 7, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t58 = gm_l3;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l3 = t61;
        }
    }
gm_lb1:;
    uint32_t t62 = gm_l1;
    gm_l6 = t62;
    uint32_t t63 = gm_l6;
    uint8_t t64 = (((uint32_t)1ULL) <= t63);
    if (t64) {
        uint32_t t65 = gm_l6;
        uint8_t t66 = (t65 <= ((uint32_t)128ULL));
        gm_l11 = t66;
    } else {
        gm_l11 = ((uint8_t)0);
    }
    uint8_t t67 = gm_l11;
    uint8_t t68 = (uint8_t)!t67;
    if (t68) {
        gm_tp8mv052i18rg t69;
        memset(&t69, 0, sizeof t69);
        F.ret = t69;
        goto gm_fret;
    }
    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t71, gm_srcsffver18si54, 12, 21);
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t70) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t73 = ((t72 == ((uint8_t)105ULL)));
    uint32_t t74 = gm_l1;
    uint16_t t75 = (uint16_t)t74;
    gm_tjcj4j2gbh6c8 t76;
    GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), t73);
    GM_ST(uint16_t, ((uint8_t *)&t76 + 2), t75);
    gm_tp8mv052i18rg t77;
    GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t77), t76);
    ((uint8_t *)&t77)[4] = 1;
    F.ret = t77;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_builtin_type_name_v5ttoka F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 453u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    gm_tp8mv052i18rg gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_tp8mv052i18rg t6 = gm_f_int_type_name_a77a0kf(t5);
    gm_l3 = t6;
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
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
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 49, 12);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    if (t8) {
        gm_l1 = ((uint8_t)1);
    } else {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_shnig5tqtdedi));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sm9e81djuncnu));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqqajgod2bb4o));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s3cv29r83pgn4));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_slolcr6ikvg33));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s0iqiubfm7e7e));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s47llp68ptjgu));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sflee3fpg0btv));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
        gm_th4d8o29g5urm t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 32), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 48), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 64), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 80), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 96), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 112), t17);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 128), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 144), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 160), t20);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 176), t21);
        uint8_t t23 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 0, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 16, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 32, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 48, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 64, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 80, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 96, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 112, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 128, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 144, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 160, ((uint8_t *)&t9))) t23 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + 176, ((uint8_t *)&t9))) t23 = 1;
        uint8_t t24 = (uint8_t)(t23);
        gm_l1 = t24;
    }
    uint8_t t25 = gm_l1;
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_oversized_int_name_nr25haf F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t13 = (t12 < ((uint64_t)2ULL));
    if (t13) {
        gm_l10 = ((uint8_t)1);
    } else {
        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t15, gm_srcsffver18si54, 17, 24);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t17 + 1), ((uint8_t)105ULL));
        uint8_t t18 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 0, ((uint8_t *)&t16))) t18 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 1, ((uint8_t *)&t16))) t18 = 1;
        uint8_t t19 = (uint8_t)(!t18);
        gm_l10 = t19;
    }
    uint8_t t20 = gm_l10;
    if (t20) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t22, gm_srcsffver18si54, 17, 53);
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t24 = ((t23 == ((uint8_t)48ULL)));
        gm_l9 = t24;
    }
    uint8_t t25 = gm_l9;
    if (t25) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t28 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t29 = (uintptr_t)t27;
    if (t28 > t29 || t29 > (uintptr_t)t27) gm_trap_slice(t28, t29, t27, gm_srcsffver18si54, 18, 14);
    uint64_t t30 = ((uintptr_t)t27) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    gm_l1 = t31;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l2;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t37 = gm_l2;
                {
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)t37 * 1));
                        gm_l3 = t38;
                        uint8_t t39 = gm_l3;
                        gm_l4 = t39;
                        uint8_t t40 = gm_l4;
                        uint8_t t41 = (((uint8_t)48ULL) <= t40);
                        if (t41) {
                            uint8_t t42 = gm_l4;
                            uint8_t t43 = (t42 <= ((uint8_t)57ULL));
                            gm_l11 = t43;
                        } else {
                            gm_l11 = ((uint8_t)0);
                        }
                        uint8_t t44 = gm_l11;
                        uint8_t t45 = (uint8_t)!t44;
                        if (t45) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l2;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l2 = t49;
        }
    }
gm_lb1:;
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t51 = (t50 > ((uint64_t)4ULL));
    if (t51) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_l5 = ((uint32_t)0ULL);
    void * t52 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t54 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t55 = (uintptr_t)t53;
    if (t54 > t55 || t55 > (uintptr_t)t53) gm_trap_slice(t54, t55, t53, gm_srcsffver18si54, 23, 14);
    uint64_t t56 = ((uintptr_t)t53) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t52 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
    gm_l6 = t57;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t58 = gm_l7;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t60 = (t58 < t59);
            if (!t60) {
                goto gm_lb6;
            }
            {
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t63 = gm_l7;
                {
                    {
                        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t61) + (uintptr_t)t63 * 1));
                        gm_l8 = t64;
                        uint32_t t65 = gm_l5;
                        uint32_t r66; bool o67 = __builtin_mul_overflow(t65, ((uint32_t)10ULL), &r66);
                        if (o67) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 23, 33);
                        uint32_t t68 = r66;
                        uint8_t t69 = gm_l8;
                        uint8_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint8_t)48ULL), &r70);
                        if (o71) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 23, 49);
                        uint8_t t72 = r70;
                        uint32_t t73 = (uint32_t)t72;
                        uint32_t r74; bool o75 = __builtin_add_overflow(t68, t73, &r74);
                        if (o75) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 23, 33);
                        uint32_t t76 = r74;
                        gm_l5 = t76;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 23, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t77 = gm_l7;
            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
            uint64_t t80 = r78;
            gm_l7 = t80;
        }
    }
gm_lb6:;
    uint32_t t81 = gm_l5;
    uint8_t t82 = (t81 > ((uint32_t)128ULL));
    F.ret = t82;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
extern __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1) { gm_f_leave_module_scope_4hp5mc4(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_len_param_names_cilm68f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* out */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l7; /* m */
    gm_b16_8 gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 8);
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l11, t12, GM_CONV(gm_csguankko4lmhd, t13));
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)19ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
            gm_l4 = t16;
            gm_b16_8 t17 = gm_l2;
            gm_l5 = t17;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t20 = (t18 < t19);
                    if (!t20) {
                        goto gm_lb3;
                    }
                    {
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t23 = gm_l6;
                        {
                            {
                                gm_tplpbqf3duvn3 t24 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
                                gm_l7 = t24;
                                uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                                uint32_t t26 = gm_l4;
                                uint8_t t27 = ((t25 == t26));
                                if (t27) {
                                    {
                                        void * t28 = gm_l0;
                                        void * t29 = (void *)(((uint8_t *)t28) + 8);
                                        uint32_t t30 = gm_l4;
                                        gm_b16_8 t31 = gm_f_param_name_rbnop5l(t29, t30);
                                        void * t32 = gm_l0;
                                        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 4));
                                        gm_b16_8 t34 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t32, t33);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t6j6smj7qoctu t35;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t31);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 16), t34);
                                        gm_f_format_vom8db6((void *)&gm_l12, (void *)NULL, cx, NULL, &t35);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 692, 29);
                                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                                    gm_l8 = t38;
                                    gm_b16_8 t39 = gm_l8;
                                    void * t40 = gm_l3;
                                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)t40));
                                    uint8_t t42 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t41) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t41))) + i * 16, ((uint8_t *)&t39))) { t42 = 1; break; }
                                    uint8_t t43 = (uint8_t)(!t42);
                                    if (t43) {
                                        void * t44 = gm_l3;
                                        gm_b16_8 t45 = gm_l8;
                                        gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
                                        gm_l13 = t46;
                                        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcsffver18si54, 693, 41);
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 690, 22);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t48 = gm_l6;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l6 = t51;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)22ULL));
            if (!t53) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 4));
            gm_l9 = t54;
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 8));
            gm_l10 = t55;
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l9;
            gm_b16_8 t58 = gm_l2;
            void * t59 = gm_l3;
            gm_b0_1 t60 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t56, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l10;
            gm_b16_8 t63 = gm_l2;
            void * t64 = gm_l3;
            gm_b0_1 t65 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t61, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 689, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_len_params_bound_f7mjvn9 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l6; /* m */
    gm_t428ue2u3hf5g gm_l7; /* k */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* b */
    GM_STACK_CHECK(&F, 461u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l10;
    gm_t53at80pfdus3 gm_l11;
    uint8_t gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint8_t gm_l14;
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l10, t17, GM_CONV(gm_csguankko4lmhd, t18));
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)19ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l3 = t21;
            gm_b16_8 t22 = gm_l2;
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
                                gm_tplpbqf3duvn3 t29 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t26) + (uintptr_t)t28 * 8));
                                gm_l6 = t29;
                                uint32_t t30 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                uint32_t t31 = gm_l3;
                                uint8_t t32 = ((t30 == t31));
                                if (t32) {
                                    {
                                        {
                                            {
                                                void * t33 = gm_l0;
                                                void * t34 = (void *)(((uint8_t *)t33) + 8);
                                                gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
                                                gm_f_kind_jcnt4hm((void *)&gm_l11, t34, GM_CONV(gm_csguankko4lmhd, t35));
                                            }
                                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)21ULL));
                                            if (!t37) {
                                                goto gm_lb9;
                                            }
                                            {
                                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l11) + 8), 32);
                                            }
                                            {
                                                gm_tf5srh4mp4b3i t38 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l7)));
                                                gm_l13 = t38;
                                            }
                                            {
                                                {
                                                    uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                                                    if (t39) {
                                                        goto gm_lb11;
                                                    }
                                                    gm_l12 = ((uint8_t)0);
                                                    goto gm_lb10;
                                                }
gm_lb11:;
                                                {
                                                    gm_l12 = ((uint8_t)1);
                                                    goto gm_lb10;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 676, 63);
                                            }
gm_lb10:;
                                            uint8_t t40 = gm_l12;
                                            F.ret = t40;
                                            goto gm_fret;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 674, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t41 = gm_l5;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l5 = t44;
                }
            }
gm_lb3:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)22ULL));
            if (!t46) {
                goto gm_lb13;
            }
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 4));
            gm_l8 = t47;
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 8));
            gm_l9 = t48;
            void * t49 = gm_l0;
            gm_t88rcucg1b7uk t50 = gm_l8;
            gm_b16_8 t51 = gm_l2;
            uint8_t t52 = gm_f_len_params_bound_f7mjvn9(t49, GM_CONV(gm_csguankko4lmhd, t50), t51);
            if (t52) {
                void * t53 = gm_l0;
                gm_t88rcucg1b7uk t54 = gm_l9;
                gm_b16_8 t55 = gm_l2;
                uint8_t t56 = gm_f_len_params_bound_f7mjvn9(t53, GM_CONV(gm_csguankko4lmhd, t54), t55);
                gm_l14 = t56;
            } else {
                gm_l14 = ((uint8_t)0);
            }
            uint8_t t57 = gm_l14;
            F.ret = t57;
            goto gm_fret;
        }
gm_lb13:;
        {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 672, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_length_is_type_soi53ah F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* len */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_t9805qtonbsag gm_l6;
    void * t9 = gm_l2;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)6ULL));
            if (!t11) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t9) + 8) + 8), 32);
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_t9805qtonbsag t14 = gm_f_value_param_tof1h6t(t12, t13);
            gm_l6 = t14;
            {
                {
                    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
                    if (t15) {
                        goto gm_lb4;
                    }
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 585, 30);
            }
gm_lb3:;
            uint8_t t16 = gm_l5;
            if (t16) {
                void * t17 = gm_l0;
                uint64_t t18 = gm_l1;
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                uint8_t t20 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t17, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t20;
            } else {
                gm_l4 = ((uint8_t)0);
            }
            uint8_t t21 = gm_l4;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)22ULL));
            if (!t23) {
                goto gm_lb6;
            }
            void * t24 = gm_l0;
            uint64_t t25 = gm_l1;
            void * t26 = gm_l2;
            uint8_t t27 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t24, t25, t26, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t27;
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 584, 5);
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
inline __attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_const_idpokjh F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1688) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 109, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1688));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1688) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 110, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 224));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1688));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1688) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 110, 58);
                    gm_tb07pp6njg0bk t24 = GM_LD(gm_tb07pp6njg0bk, (((uint8_t *)t21) + (uintptr_t)t23 * 224));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 225, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 226, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 228, 0);
                    GM_ST(gm_tb07pp6njg0bk, ((uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out)), t24);
                    ((uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out))[224] = 1;
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    memset(&(*(gm_tk1gen3hgkm51 *)gm_out), 0, sizeof (*(gm_tk1gen3hgkm51 *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1) { gm_tk1gen3hgkm51 gm_r; gm_f_local_const_idpokjh(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_fn_aji8qnd F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1728) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 119, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1728));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1728) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 120, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1728));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1728) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 120, 55);
                    uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_t9805qtonbsag t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_looks_like_type_ho5qv1k F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    gm_t3mf58gv9kf3n gm_l6; /* a */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* lhs */
    void * gm_l13; /* callee */
    gm_tkb8hpcqf70t1 gm_l14; /* g */
    void * gm_l15; /* inner */
    GM_STACK_CHECK(&F, 1200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l16;
    uint8_t gm_l17;
    gm_t6qooom8qa2do gm_l18;
    uint8_t gm_l19;
    void * gm_l20;
    uint8_t gm_l21;
    void * gm_l22;
    uint8_t gm_l23;
    gm_toblvhepnjqkh gm_l24;
    uint8_t gm_l25;
    gm_t6qooom8qa2do gm_l26;
    void * t41 = gm_l2;
    {
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)6ULL));
            if (!t43) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t41) + 8) + 8), 32);
            {
                {
                    void * t44 = gm_l0;
                    uint64_t t45 = gm_l1;
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_tf5srh4mp4b3i t47 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t44, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t47;
                    uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                    if (!t48) {
                        goto gm_lb4;
                    }
                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                    gm_l4 = t49;
                    void * t50 = gm_l0;
                    uint64_t t51 = gm_l4;
                    gm_f_resolve_decl_pus042v((void *)&gm_l18, (void *)NULL, cx, NULL, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)0ULL));
                            if (!t53) {
                                goto gm_lb6;
                            }
                            gm_l17 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l17 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 221, 68);
                    }
gm_lb5:;
                    uint8_t t54 = gm_l17;
                    F.ret = t54;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            uint8_t t56 = gm_f_is_builtin_type_name_v5ttoka(t55);
            F.ret = t56;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)9ULL));
            if (!t58) {
                goto gm_lb8;
            }
            void * t59 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l5 = t59;
            void * t60 = gm_l0;
            uint64_t t61 = gm_l1;
            void * t62 = gm_l5;
            uint8_t t63 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t63;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)28ULL));
            if (!t65) {
                goto gm_lb9;
            }
            gm_t3mf58gv9kf3n t66 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t41) + 8) + 8));
            gm_l6 = t66;
            void * t67 = gm_l2;
            uint8_t t68 = gm_f_hoists_13k2a2s(t67);
            if (t68) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t69 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
            void * t70 = gm_f_construction_call_ktqtrpo(t69);
            gm_l20 = t70;
            {
                {
                    uint8_t t71 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l20)) != 0);
                    if (t71) {
                        goto gm_lb11;
                    }
                    gm_l19 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l19 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 230, 16);
            }
gm_lb10:;
            uint8_t t72 = gm_l19;
            if (t72) {
                void * t73 = gm_l0;
                uint64_t t74 = gm_l1;
                void * t75 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                uint8_t t76 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t76;
                goto gm_fret;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)24ULL));
            if (!t78) {
                goto gm_lb13;
            }
            void * t79 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l7 = t79;
            void * t80 = gm_l2;
            uint8_t t81 = gm_f_hoists_13k2a2s(t80);
            if (t81) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t82 = gm_l7;
            void * t83 = gm_f_construction_call_ktqtrpo(t82);
            gm_l22 = t83;
            {
                {
                    uint8_t t84 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l22)) != 0);
                    if (t84) {
                        goto gm_lb15;
                    }
                    gm_l21 = ((uint8_t)0);
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    gm_l21 = ((uint8_t)1);
                    goto gm_lb14;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 235, 16);
            }
gm_lb14:;
            uint8_t t85 = gm_l21;
            if (t85) {
                void * t86 = gm_l0;
                uint64_t t87 = gm_l1;
                void * t88 = gm_l7;
                uint8_t t89 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t86, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t89;
                goto gm_fret;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)10ULL));
            if (!t91) {
                goto gm_lb17;
            }
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t41) + 8) + 8));
            gm_l8 = t92;
            gm_b16_8 t93 = gm_l8;
            gm_l9 = t93;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t94 = gm_l10;
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb18;
                    }
                    {
                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t99 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)t97) + (uintptr_t)t99 * 136), 136);
                                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 128));
                                if (t100) {
                                    gm_l23 = ((uint8_t)1);
                                } else {
                                    void * t101 = gm_l0;
                                    uint64_t t102 = gm_l1;
                                    void * t103 = (void *)(((uint8_t *)&F.l11) + 40);
                                    uint8_t t104 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t101, t102, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t105 = (uint8_t)!t104;
                                    gm_l23 = t105;
                                }
                                uint8_t t106 = gm_l23;
                                if (t106) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 239, 13);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t107 = gm_l10;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    uint64_t t110 = r108;
                    gm_l10 = t110;
                }
            }
gm_lb18:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb17:;
        {
            {
                {
                    uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)25ULL));
                    if (!t112) {
                        goto gm_lb25;
                    }
                    goto gm_lb24;
                }
gm_lb25:;
                {
                    uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t114 = (uint8_t)(t113 == ((uint8_t)26ULL));
                    if (!t114) {
                        goto gm_lb26;
                    }
                    goto gm_lb24;
                }
gm_lb26:;
                {
                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)27ULL));
                    if (!t116) {
                        goto gm_lb27;
                    }
                    goto gm_lb24;
                }
gm_lb27:;
                {
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)29ULL));
                    if (!t118) {
                        goto gm_lb28;
                    }
                    goto gm_lb24;
                }
gm_lb28:;
                {
                    uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t120 = (uint8_t)(t119 == ((uint8_t)30ULL));
                    if (!t120) {
                        goto gm_lb29;
                    }
                    goto gm_lb24;
                }
gm_lb29:;
                {
                    uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)31ULL));
                    if (!t122) {
                        goto gm_lb30;
                    }
                    goto gm_lb24;
                }
gm_lb30:;
                {
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)32ULL));
                    if (!t124) {
                        goto gm_lb31;
                    }
                    goto gm_lb24;
                }
gm_lb31:;
                {
                    uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t126 = (uint8_t)(t125 == ((uint8_t)33ULL));
                    if (!t126) {
                        goto gm_lb32;
                    }
                    goto gm_lb24;
                }
gm_lb32:;
                goto gm_lb23;
            }
gm_lb24:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb23:;
        {
            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t128 = (uint8_t)(t127 == ((uint8_t)17ULL));
            if (!t128) {
                goto gm_lb33;
            }
            void * t129 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l12 = t129;
            void * t130 = gm_l0;
            uint64_t t131 = gm_l1;
            void * t132 = gm_l12;
            uint8_t t133 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t130, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t133;
            goto gm_fret;
        }
gm_lb33:;
        {
            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t135 = (uint8_t)(t134 == ((uint8_t)19ULL));
            if (!t135) {
                goto gm_lb34;
            }
            void * t136 = GM_LD(void *, ((((uint8_t *)t41) + 8) + 8));
            gm_l13 = t136;
            {
                void * t137 = gm_l0;
                uint64_t t138 = gm_l1;
                void * t139 = gm_l13;
                gm_f_generic_decl_oir9hm6((void *)&gm_l24, (void *)NULL, cx, NULL, t137, t138, t139, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t140 = (uint8_t)((((uint8_t *)&gm_l24))[24] != 0);
            if (t140) {
                memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l24), 24);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                void * t141 = gm_l0;
                uint64_t t142 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                gm_f_resolve_decl_pus042v((void *)&gm_l26, (void *)NULL, cx, NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)1ULL));
                    if (!t144) {
                        goto gm_lb36;
                    }
                    gm_l25 = ((uint8_t)1);
                    goto gm_lb35;
                }
gm_lb36:;
                {
                    gm_l25 = ((uint8_t)0);
                    goto gm_lb35;
                }
gm_lb37:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 248, 20);
            }
gm_lb35:;
            uint8_t t145 = gm_l25;
            F.ret = t145;
            goto gm_fret;
        }
gm_lb34:;
        {
            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)35ULL));
            if (!t147) {
                goto gm_lb38;
            }
            void * t148 = GM_LD(void *, (((((uint8_t *)t41) + 8) + 8) + 16));
            gm_l15 = t148;
            void * t149 = gm_l0;
            uint64_t t150 = gm_l1;
            void * t151 = gm_l15;
            uint8_t t152 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t149, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t152;
            goto gm_fret;
        }
gm_lb38:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb39:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 219, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_names_error_oet8t6b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 583u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l5;
    {
        {
            {
                void * t4 = gm_l2;
                uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
                uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
                if (!t6) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t4) + 8) + 8), 32);
            }
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_tf5srh4mp4b3i t10 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t10;
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
            if (t11) {
                uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                gm_l4 = t12;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                void * t13 = gm_l0;
                uint64_t t14 = gm_l4;
                gm_t6qooom8qa2do t15;
                gm_f_resolve_decl_pus042v((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
                uint8_t t17 = (uint8_t)((t16 == ((uint8_t)11ULL)));
                gm_t6qooom8qa2do t18;
                memset(&t18, 0, sizeof t18);
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)11ULL));
                F.ret = t17;
                goto gm_fret;
            }
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_names_type_rjdgib9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tkn42u7na3t81 gm_l5;
    gm_tf5srh4mp4b3i gm_l6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l2;
    gm_tkn42u7na3t81 t9 = gm_f_type_param_3av4r8i(t7, t8);
    gm_l5 = t9;
    {
        {
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
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
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 138, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l4;
    if (t11) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            void * t12 = gm_l0;
            uint64_t t13 = gm_l1;
            gm_b16_8 t14 = gm_l2;
            gm_tf5srh4mp4b3i t15 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t15;
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
            if (!t16) {
                goto gm_lb5;
            }
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l3 = t17;
            void * t18 = gm_l0;
            uint64_t t19 = gm_l3;
            uint8_t t20 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t18, t19, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t20;
            goto gm_fret;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_b16_8 t21 = gm_l2;
    uint8_t t22 = gm_f_is_builtin_type_name_v5ttoka(t21);
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
inline __attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_operand_3n5jchh F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l6;
    void * t11 = gm_l0;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)28ULL));
            if (!t13) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t14 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t11) + 8) + 8));
            gm_l1 = t14;
            void * t15 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
            void * t16 = gm_f_prefix_operand_3n5jchh(t15);
            gm_l6 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)24ULL));
            if (!t18) {
                goto gm_lb3;
            }
            void * t19 = GM_LD(void *, ((((uint8_t *)t11) + 8) + 8));
            gm_l2 = t19;
            void * t20 = gm_l2;
            void * t21 = gm_f_prefix_operand_3n5jchh(t20);
            gm_l6 = t21;
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)25ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    void * t24 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t24;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)26ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    void * t27 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t27;
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)27ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    void * t30 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
                    gm_l3 = t30;
                    goto gm_lb5;
                }
gm_lb8:;
                goto gm_lb4;
            }
gm_lb5:;
            void * t31 = gm_l3;
            void * t32 = gm_f_prefix_operand_3n5jchh(t31);
            gm_l6 = t32;
            goto gm_lb1;
        }
gm_lb4:;
        {
            void * t33 = gm_l0;
            gm_l6 = t33;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 295, 64);
    }
gm_lb1:;
    void * t34 = gm_l6;
    F.ret = t34;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_push_down_hge098p F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* inner */
    void * gm_l2; /* b */
    void * gm_l3; /* b */
    void * gm_l4; /* b */
    void * gm_l5; /* b */
    void * gm_l6; /* b */
    void * gm_l7; /* b */
    gm_tao63pk3r18v5 gm_l8; /* x */
    void * gm_l9; /* callee */
    void * gm_l10; /* base */
    void * gm_l11; /* next */
    void * gm_l12; /* callee */
    void * gm_l13; /* down */
    void * gm_l14; /* i */
    gm_t08olq97rscn3 gm_l15; /* name */
    uint32_t gm_l16; /* i */
    gm_tl874t2atnlh3 gm_l17; /* at */
    gm_tao63pk3r18v5 gm_l18; /* x */
    gm_b16_8 gm_l19; /* args */
    uint8_t gm_l20; /* trailing */
    GM_STACK_CHECK(&F, 1233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * gm_l22;
    gm_tq7s98ihonkju gm_l23;
    void * gm_l24;
    gm_tq7s98ihonkju gm_l25;
    gm_tq7s98ihonkju gm_l26;
    void * t26 = gm_l1;
    {
        {
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)20ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    void * t29 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t29;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)22ULL));
                    if (!t31) {
                        goto gm_lb5;
                    }
                    void * t32 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t32;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)15ULL));
                    if (!t34) {
                        goto gm_lb6;
                    }
                    void * t35 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t35;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)16ULL));
                    if (!t37) {
                        goto gm_lb7;
                    }
                    void * t38 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t38;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)18ULL));
                    if (!t40) {
                        goto gm_lb8;
                    }
                    void * t41 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l2 = t41;
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t42 = gm_l2;
            gm_l10 = t42;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)23ULL));
            if (!t44) {
                goto gm_lb9;
            }
            void * t45 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l7 = t45;
            void * t46 = gm_l7;
            gm_l10 = t46;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)21ULL));
            if (!t48) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t26) + 8) + 8), 32);
            void * t49 = GM_LD(void *, ((uint8_t *)&gm_l8));
            gm_l10 = t49;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)19ULL));
            if (!t51) {
                goto gm_lb11;
            }
            void * t52 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l9 = t52;
            void * t53 = gm_l9;
            uint8_t t54 = gm_f_calls_on_value_q0pa6h5(t53);
            if (t54) {
                void * t55 = gm_l9;
                gm_l22 = t55;
            } else {
                F.ret = ((void *)0);
                goto gm_fret;
                gm_l22 = ((void *)0);
            }
            void * t56 = gm_l22;
            gm_l10 = t56;
            goto gm_lb1;
        }
gm_lb11:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 886, 13);
    }
gm_lb1:;
    {
        void * t57 = gm_l10;
        gm_tq7s98ihonkju t58 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t57)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knjqrvnblc8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t58;
    }
    uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcsffver18si54, 893, 23);
    void * t60 = GM_LD(void *, (((uint8_t *)&gm_l23) + 8));
    gm_l11 = t60;
    {
        {
            void * t61 = gm_l10;
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t61) + 8) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)19ULL));
            if (!t63) {
                goto gm_lb14;
            }
            void * t64 = GM_LD(void *, ((((uint8_t *)t61) + 8) + 8));
            gm_l12 = t64;
            void * t65 = gm_l12;
            uint8_t t66 = gm_f_calls_on_value_q0pa6h5(t65);
            uint8_t t67 = (uint8_t)!t66;
            if (!t67) {
                goto gm_lb14;
            }
            void * t68 = gm_l0;
            void * t69 = gm_l10;
            void * t70 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t70;
            void * t71 = gm_l11;
            void * t72 = gm_l0;
            gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
            void * t74 = gm_l10;
            gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 72));
            gm_tl874t2atnlh3 t76 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t73), GM_CONV(gm_cseahvg8v7p0vs, t75)));
            GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72), t76);
            goto gm_lb13;
        }
gm_lb14:;
        void * t77 = gm_l0;
        void * t78 = gm_l10;
        void * t79 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t79;
        uint8_t t80 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l24)) != 0);
        if (t80) {
            void * t81 = GM_LD(void *, ((uint8_t *)&gm_l24));
            gm_l13 = t81;
        } else {
            F.ret = ((void *)0);
            goto gm_fret;
        }
        {
            void * t82 = gm_l13;
            gm_tq7s98ihonkju t83 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t82)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kcodas3apkm)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t83;
        }
        uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_srcsffver18si54, 899, 16);
        void * t85 = GM_LD(void *, (((uint8_t *)&gm_l25) + 8));
        gm_l11 = t85;
    }
gm_lb13:;
    void * t86 = gm_l1;
    {
        {
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)20ULL));
            if (!t88) {
                goto gm_lb16;
            }
            void * t89 = GM_LD(void *, (((((uint8_t *)t86) + 8) + 8) + 8));
            gm_l14 = t89;
            void * t90 = gm_l11;
            void * t91 = gm_l14;
            gm_t3mf58gv9kf3n t92;
            GM_ST(void *, ((uint8_t *)&t92 + 0), t90);
            GM_ST(void *, ((uint8_t *)&t92 + 8), t91);
            memset(&F.l21, 0, sizeof F.l21);
            GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)20ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&F.l21 + 8), t92);
            goto gm_lb15;
        }
gm_lb16:;
        {
            uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t94 = (uint8_t)(t93 == ((uint8_t)22ULL));
            if (!t94) {
                goto gm_lb17;
            }
            {
                memmove(((uint8_t *)&gm_l15), (((((uint8_t *)t86) + 8) + 8) + 8), 32);
            }
            {
                void * t95 = gm_l11;
                gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l15));
                gm_tioie2ju3q4ul t97;
                GM_ST(void *, ((uint8_t *)&t97 + 0), t95);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t97 + 8), t96);
                GM_ST(uint8_t, (uint8_t *)&F.l21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l21 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)22ULL));
                GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&F.l21 + 8), t97);
            }
            goto gm_lb15;
        }
gm_lb17:;
        {
            uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)15ULL));
            if (!t99) {
                goto gm_lb18;
            }
            void * t100 = gm_l11;
            memset(&F.l21, 0, sizeof F.l21);
            GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)15ULL));
            GM_ST(void *, ((uint8_t *)&F.l21 + 8), t100);
            goto gm_lb15;
        }
gm_lb18:;
        {
            uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)16ULL));
            if (!t102) {
                goto gm_lb19;
            }
            void * t103 = gm_l11;
            memset(&F.l21, 0, sizeof F.l21);
            GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)16ULL));
            GM_ST(void *, ((uint8_t *)&F.l21 + 8), t103);
            goto gm_lb15;
        }
gm_lb19:;
        {
            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)18ULL));
            if (!t105) {
                goto gm_lb20;
            }
            void * t106 = gm_l11;
            memset(&F.l21, 0, sizeof F.l21);
            GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&F.l21 + 8), t106);
            goto gm_lb15;
        }
gm_lb20:;
        {
            uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)23ULL));
            if (!t108) {
                goto gm_lb21;
            }
            uint32_t t109 = GM_LD(uint32_t, (((((uint8_t *)t86) + 8) + 8) + 8));
            gm_l16 = t109;
            gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t86) + 8) + 8) + 12));
            gm_l17 = t110;
            {
                void * t111 = gm_l11;
                uint32_t t112 = gm_l16;
                gm_tl874t2atnlh3 t113 = gm_l17;
                gm_tupk62cr6lbdu t114;
                GM_ST(void *, ((uint8_t *)&t114 + 0), t111);
                GM_ST(uint32_t, ((uint8_t *)&t114 + 8), t112);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t114 + 12), t113);
                memset(&F.l21, 0, sizeof F.l21);
                GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)23ULL));
                GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&F.l21 + 8), t114);
            }
            goto gm_lb15;
        }
gm_lb21:;
        {
            uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)21ULL));
            if (!t116) {
                goto gm_lb22;
            }
            {
                memmove(((uint8_t *)&gm_l18), ((((uint8_t *)t86) + 8) + 8), 32);
            }
            {
                void * t117 = gm_l11;
                void * t118 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
                void * t119 = GM_LD(void *, (((uint8_t *)&gm_l18) + 16));
                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 24));
                gm_tao63pk3r18v5 t121;
                GM_ST(uint8_t, (uint8_t *)&t121 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t121 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t121 + 28, 0);
                GM_ST(void *, ((uint8_t *)&t121 + 0), t117);
                GM_ST(void *, ((uint8_t *)&t121 + 8), t118);
                GM_ST(void *, ((uint8_t *)&t121 + 16), t119);
                GM_ST(uint8_t, ((uint8_t *)&t121 + 24), t120);
                GM_ST(uint8_t, (uint8_t *)&F.l21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l21 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&F.l21 + 8), t121);
            }
            goto gm_lb15;
        }
gm_lb22:;
        {
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            uint8_t t123 = (uint8_t)(t122 == ((uint8_t)19ULL));
            if (!t123) {
                goto gm_lb23;
            }
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((((uint8_t *)t86) + 8) + 8) + 8));
            gm_l19 = t124;
            uint8_t t125 = GM_LD(uint8_t, (((((uint8_t *)t86) + 8) + 8) + 24));
            gm_l20 = t125;
            {
                void * t126 = gm_l11;
                gm_b16_8 t127 = gm_l19;
                uint8_t t128 = gm_l20;
                gm_tlljg61a2ctgq t129;
                GM_ST(uint8_t, (uint8_t *)&t129 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t129 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t129 + 28, 0);
                GM_ST(void *, ((uint8_t *)&t129 + 0), t126);
                GM_ST(gm_b16_8, ((uint8_t *)&t129 + 8), t127);
                GM_ST(uint8_t, ((uint8_t *)&t129 + 24), t128);
                GM_ST(uint8_t, (uint8_t *)&F.l21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l21 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l21 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&F.l21 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&F.l21 + 8), t129);
            }
            goto gm_lb15;
        }
gm_lb23:;
        {
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 901, 26);
    }
gm_lb15:;
    {
        void * t130 = gm_l1;
        uint32_t t131 = GM_LD(uint32_t, ((uint8_t *)t130));
        void * t132 = gm_l1;
        gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
        gm_t2gtbh39ptekd t134 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l21));
        gm_tc3oht4ksth9d t135;
        GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t135 + 84, 0);
        GM_ST(uint32_t, ((uint8_t *)&t135 + 0), t131);
        GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t135 + 8), t134);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t135 + 72), t133);
        gm_tq7s98ihonkju t136 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t135, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k647t05rm86)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l26 = t136;
    }
    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcsffver18si54, 912, 12);
    void * t138 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
    F.ret = t138;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pushes_down_0djaflp F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* base */
    void * gm_l6; /* base */
    gm_tao63pk3r18v5 gm_l7; /* x */
    void * gm_l8; /* callee */
    void * gm_l9; /* next */
    void * gm_l10; /* callee */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l11;
    void * t16 = gm_l0;
    {
        {
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)20ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    void * t19 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t19;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)22ULL));
                    if (!t21) {
                        goto gm_lb5;
                    }
                    void * t22 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t22;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)15ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    void * t25 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t25;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)16ULL));
                    if (!t27) {
                        goto gm_lb7;
                    }
                    void * t28 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t28;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)18ULL));
                    if (!t30) {
                        goto gm_lb8;
                    }
                    void * t31 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
                    gm_l1 = t31;
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t32 = gm_l1;
            gm_l9 = t32;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)23ULL));
            if (!t34) {
                goto gm_lb9;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l6 = t35;
            void * t36 = gm_l6;
            gm_l9 = t36;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)21ULL));
            if (!t38) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t16) + 8) + 8), 32);
            void * t39 = GM_LD(void *, ((uint8_t *)&gm_l7));
            gm_l9 = t39;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)19ULL));
            if (!t41) {
                goto gm_lb11;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l8 = t42;
            void * t43 = gm_l8;
            uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
            if (t44) {
                void * t45 = gm_l8;
                gm_l11 = t45;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
                gm_l11 = ((void *)0);
            }
            void * t46 = gm_l11;
            gm_l9 = t46;
            goto gm_lb1;
        }
gm_lb11:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 303, 13);
    }
gm_lb1:;
    {
        {
            void * t47 = gm_l9;
            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)19ULL));
            if (!t49) {
                goto gm_lb14;
            }
            void * t50 = GM_LD(void *, ((((uint8_t *)t47) + 8) + 8));
            gm_l10 = t50;
            void * t51 = gm_l10;
            uint8_t t52 = gm_f_calls_on_value_q0pa6h5(t51);
            uint8_t t53 = (uint8_t)!t52;
            if (!t53) {
                goto gm_lb14;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb14:;
    }
gm_lb13:;
    void * t54 = gm_l9;
    uint8_t t55 = gm_f_pushes_down_0djaflp(t54);
    F.ret = t55;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_jjodbei F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint8_t gm_l4; /* sized */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l1;
    void * t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = gm_f_resolve_type_unchecked_psme7dn((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t15;
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_l3;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t17, GM_CONV(gm_csguankko4lmhd, t18));
    {
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)11ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)12ULL));
                    if (!t22) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)13ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)14ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 320, 14);
    }
gm_lb1:;
    {
        {
            uint8_t t27 = gm_l4;
            if (!t27) {
                goto gm_lb10;
            }
            void * t28 = gm_l0;
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t28) + 1848) + 8));
            uint8_t t30 = ((t29 == ((uint64_t)0ULL)));
            if (!t30) {
                goto gm_lb10;
            }
            void * t31 = gm_l0;
            uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)t31) + 1936));
            uint8_t t33 = ((t32 == ((uint32_t)0ULL)));
            if (!t33) {
                goto gm_lb10;
            }
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l3;
            void * t36 = gm_l2;
            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
            uint8_t t38 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t34, t35, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = (uint8_t)!t38;
            if (!t39) {
                goto gm_lb10;
            }
            void * t40 = gm_l0;
            void * t41 = (void *)(((uint8_t *)t40) + 8);
            gm_t88rcucg1b7uk t42 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t42;
            goto gm_fret;
        }
gm_lb10:;
    }
gm_lb9:;
    gm_t88rcucg1b7uk t43 = gm_l3;
    F.ret = t43;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_unchecked_psme7dn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* tys */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* t */
    void * gm_l10; /* inner */
    gm_b16_8 gm_l11; /* elems */
    uint8_t gm_l12; /* named */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t08olq97rscn3 gm_l21; /* n */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* n */
    void * gm_l26; /* inner */
    uint8_t gm_l27; /* is_const */
    void * gm_l28; /* inner */
    void * gm_l29; /* sig */
    uint8_t gm_l31; /* is_const */
    void * gm_l32; /* inner */
    uint8_t gm_l33; /* is_const */
    void * gm_l34; /* inner */
    gm_t3mf58gv9kf3n gm_l35; /* a */
    gm_t88rcucg1b7uk gm_l36; /* elem */
    void * gm_l37; /* len */
    gm_t88rcucg1b7uk gm_l38; /* index */
    uint64_t gm_l39; /* n */
    gm_t88rcucg1b7uk gm_l40; /* l */
    void * gm_l41; /* err */
    void * gm_l42; /* ok */
    void * gm_l43; /* err */
    void * gm_l44; /* ok */
    gm_t88rcucg1b7uk gm_l45; /* et */
    gm_t88rcucg1b7uk gm_l46; /* ot */
    void * gm_l47; /* sig */
    void * gm_l49; /* inner */
    void * gm_l50; /* callee */
    gm_b16_8 gm_l51; /* args */
    void * gm_l52; /* base */
    gm_t9805qtonbsag gm_l53; /* template */
    gm_tkb8hpcqf70t1 gm_l54; /* g */
    uint32_t gm_l55; /* t */
    gm_t08olq97rscn3 gm_l56; /* id */
    uint32_t gm_l57; /* t */
    gm_b16_8 gm_l58; /* targs */
    gm_b16_8 gm_l59; /* params */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    uint64_t gm_l62; /* i */
    uint32_t gm_l63; /* pid */
    gm_t88rcucg1b7uk gm_l64; /* a */
    gm_b16_8 gm_l65; /* <tmp> */
    uint64_t gm_l66; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l67; /* c */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    gm_t88rcucg1b7uk gm_l70; /* a */
    gm_t88rcucg1b7uk gm_l71; /* part */
    gm_t88rcucg1b7uk gm_l72; /* inst */
    gm_b16_8 gm_l73; /* attrs */
    void * gm_l74; /* inner */
    void * gm_l75; /* f */
    gm_b16_8 gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    gm_b16_8 gm_l79; /* name */
    gm_b16_8 gm_l81; /* args */
    void * gm_l83; /* hook */
    void * gm_l84; /* base */
    gm_t08olq97rscn3 gm_l85; /* name */
    uint64_t gm_l86; /* m */
    uint64_t gm_l87; /* d */
    gm_t88rcucg1b7uk gm_l88; /* t */
    GM_STACK_CHECK(&F, 7606u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l89;
    gm_t53at80pfdus3 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tf5srh4mp4b3i gm_l92;
    gm_t6qooom8qa2do gm_l93;
    gm_tabb710d9mq2e gm_l94;
    gm_tkn42u7na3t81 gm_l95;
    uint8_t gm_l96;
    uint8_t gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_tftb1odc739t5 gm_l99;
    gm_tf1bjg81u0glv gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_tg93b272d11vo gm_l102;
    gm_t5e6kkuq21nig gm_l103;
    gm_tg93b272d11vo gm_l104;
    gm_toblvhepnjqkh gm_l105;
    gm_t6qooom8qa2do gm_l106;
    uint8_t gm_l107;
    uint8_t gm_l108;
    gm_tf5srh4mp4b3i gm_l109;
    uint8_t gm_l110;
    gm_tkn42u7na3t81 gm_l111;
    gm_th2lqbo77pvsc gm_l112;
    gm_tn68el8d14vku gm_l113;
    gm_tftb1odc739t5 gm_l114;
    gm_tkn42u7na3t81 gm_l115;
    uint8_t gm_l116;
    uint8_t gm_l117;
    uint8_t gm_l118;
    uint8_t gm_l119;
    uint8_t gm_l120;
    uint8_t gm_l121;
    uint8_t gm_l122;
    gm_tf5srh4mp4b3i gm_l123;
    gm_tf5srh4mp4b3i gm_l124;
    gm_t6qooom8qa2do gm_l125;
    gm_tabb710d9mq2e gm_l126;
    {
        void * t135 = gm_l0;
        void * t136 = (void *)(((uint8_t *)t135) + 8);
        gm_l3 = t136;
        void * t137 = gm_l2;
        {
            {
                {
                    uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)6ULL));
                    if (!t139) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t137) + 8) + 8), 32);
                    {
                        {
                            void * t140 = gm_l0;
                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tkn42u7na3t81 t142 = gm_f_type_param_3av4r8i(t140, t141);
                            gm_l89 = t142;
                            uint8_t t143 = (uint8_t)((((uint8_t *)&gm_l89))[4] != 0);
                            if (!t143) {
                                goto gm_lb4;
                            }
                            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l89));
                            gm_l5 = t144;
                            {
                                {
                                    {
                                        void * t145 = gm_l3;
                                        gm_t88rcucg1b7uk t146 = gm_l5;
                                        gm_f_kind_jcnt4hm((void *)&gm_l90, t145, GM_CONV(gm_csguankko4lmhd, t146));
                                    }
                                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)19ULL));
                                    if (!t148) {
                                        goto gm_lb6;
                                    }
                                    uint32_t t149 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 8));
                                    gm_l6 = t149;
                                    void * t150 = gm_l0;
                                    void * t151 = GM_LD(void *, (((uint8_t *)t150) + 1128));
                                    uint64_t t152 = GM_LD(uint64_t, ((((uint8_t *)t150) + 1128) + 8));
                                    uint32_t t153 = gm_l6;
                                    uint64_t t154 = (uint64_t)t153;
                                    if ((uintptr_t)t154 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t154, t152, gm_srcsffver18si54, 335, 49);
                                    gm_tnnjeerolos0a t155 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t151) + (uintptr_t)t154 * 48));
                                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t155) + 0));
                                    uint8_t t157 = (uint8_t)((t156 == ((uint8_t)1ULL)));
                                    if (!t157) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t158 = gm_l0;
                                        void * t159 = gm_l2;
                                        gm_tl874t2atnlh3 t160 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t159) + 72));
                                        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                        gm_tnpifv20irnph t162;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t162 + 0), t161);
                                        gm_f_format_o5o5km2((void *)&gm_l91, (void *)NULL, cx, NULL, t162);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                        if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcsffver18si54, 336, 37);
                                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                                        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                                        gm_b0_1 t166 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t158, t160, t165);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t167 = gm_l3;
                                    gm_t88rcucg1b7uk t168 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t167);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t168;
                                    goto gm_fret;
                                }
gm_lb6:;
                            }
gm_lb5:;
                            gm_t88rcucg1b7uk t169 = gm_l5;
                            F.ret = t169;
                            goto gm_fret;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    {
                        {
                            void * t170 = gm_l0;
                            uint64_t t171 = gm_l1;
                            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tf5srh4mp4b3i t173 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t170, t171, t172);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l92 = t173;
                            uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l92))[8] != 0);
                            if (!t174) {
                                goto gm_lb8;
                            }
                            uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)&gm_l92));
                            gm_l7 = t175;
                            void * t176 = gm_l0;
                            uint64_t t177 = gm_l7;
                            gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                            gm_b0_1 t179 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t176, t177, t178);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t180 = gm_l0;
                            uint64_t t181 = gm_l7;
                            gm_f_resolve_decl_pus042v((void *)&gm_l93, (void *)NULL, cx, NULL, t180, t181);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l93) + 0));
                                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)0ULL));
                                    if (!t183) {
                                        goto gm_lb10;
                                    }
                                    gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l93) + 8));
                                    gm_l8 = t184;
                                    gm_t88rcucg1b7uk t185 = gm_l8;
                                    F.ret = t185;
                                    goto gm_fret;
                                }
gm_lb10:;
                                {
                                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l93) + 0));
                                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)11ULL));
                                    if (!t187) {
                                        goto gm_lb11;
                                    }
                                    void * t188 = gm_l3;
                                    gm_t88rcucg1b7uk t189 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t188);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t189;
                                    goto gm_fret;
                                }
gm_lb11:;
                                {
                                    void * t190 = gm_l0;
                                    void * t191 = gm_l2;
                                    gm_tl874t2atnlh3 t192 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t191) + 72));
                                    gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                    gm_tnpifv20irnph t194;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t193);
                                    gm_f_format_sv91363((void *)&gm_l94, (void *)NULL, cx, NULL, t194);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                    if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcsffver18si54, 347, 41);
                                    gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                                    gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                                    gm_b0_1 t198 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t190, t192, t197);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t199 = gm_l3;
                                    gm_t88rcucg1b7uk t200 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t199);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t200;
                                    goto gm_fret;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 343, 17);
                            }
gm_lb9:;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    {
                        {
                            void * t201 = gm_l0;
                            gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                            gm_tkn42u7na3t81 t203 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t201, t202);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l95 = t203;
                            uint8_t t204 = (uint8_t)((((uint8_t *)&gm_l95))[4] != 0);
                            if (!t204) {
                                goto gm_lb14;
                            }
                            gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l95));
                            gm_l9 = t205;
                            gm_t88rcucg1b7uk t206 = gm_l9;
                            F.ret = t206;
                            goto gm_fret;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    void * t207 = gm_l0;
                    uint64_t t208 = gm_l1;
                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    void * t210 = gm_l2;
                    gm_tl874t2atnlh3 t211 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t210) + 72));
                    gm_b0_1 t212 = gm_f_unknown_type_beat5io((void *)NULL, cx, NULL, t207, t208, t209, t211);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t213 = gm_l3;
                    gm_t88rcucg1b7uk t214 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t213);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t214;
                    goto gm_fret;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t216 = (uint8_t)(t215 == ((uint8_t)9ULL));
                    if (!t216) {
                        goto gm_lb15;
                    }
                    void * t217 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l10 = t217;
                    void * t218 = gm_l0;
                    uint64_t t219 = gm_l1;
                    void * t220 = gm_l10;
                    gm_t88rcucg1b7uk t221 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t218, t219, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t221;
                    goto gm_fret;
                }
gm_lb15:;
                {
                    uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)10ULL));
                    if (!t223) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 8) + 8));
                    gm_l11 = t224;
                    gm_b16_8 t225 = gm_l11;
                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                    uint8_t t227 = gm_f_has_spread_bndtpd6(t226);
                    if (t227) {
                        void * t228 = gm_l0;
                        void * t229 = gm_l2;
                        gm_tl874t2atnlh3 t230 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t229) + 72));
                        gm_b16_8 t231;
                        GM_ST(void *, (uint8_t *)&t231, (void *)(gm_shbaullahutpe));
                        GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(36));
                        gm_b0_1 t232 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t228, t230, t231);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t233 = gm_l3;
                        gm_t88rcucg1b7uk t234 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t233);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t234;
                        goto gm_fret;
                    }
                    uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t236 = (t235 > ((uint64_t)0ULL));
                    if (t236) {
                        void * t237 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        {
                            {
                                uint8_t t239 = (uint8_t)(((((uint8_t *)t237) + (uintptr_t)((uint64_t)0ULL) * 136))[32] != 0);
                                if (t239) {
                                    goto gm_lb18;
                                }
                                gm_l97 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l97 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 362, 40);
                        }
gm_lb17:;
                        uint8_t t240 = gm_l97;
                        gm_l96 = t240;
                    } else {
                        gm_l96 = ((uint8_t)0);
                    }
                    uint8_t t241 = gm_l96;
                    gm_l12 = t241;
                    {
                        gm_trqfvmjtt4sij t242;
                        gm_f_list_nof7vfj((void *)&t242, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l13 = t242;
                    }
                    gm_b16_8 t243 = gm_l11;
                    gm_l14 = t243;
                    gm_l15 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t244 = gm_l15;
                            uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t246 = (t244 < t245);
                            if (!t246) {
                                goto gm_lb20;
                            }
                            {
                                void * t247 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint64_t t249 = gm_l15;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l16), (((uint8_t *)t247) + (uintptr_t)t249 * 136), 136);
                                        void * t250 = (void *)((uint8_t *)&F.l13);
                                        void * t251 = gm_l0;
                                        uint64_t t252 = gm_l1;
                                        void * t253 = (void *)(((uint8_t *)&F.l16) + 40);
                                        gm_t88rcucg1b7uk t254 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t251, t252, t253);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t255 = gm_f_push_k3l9nic(t250, GM_CONV(gm_csguankko4lmhd, t254));
                                        gm_l98 = t255;
                                        uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                        if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srcsffver18si54, 364, 31);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 364, 13);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t257 = gm_l15;
                            uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                            uint64_t t260 = r258;
                            gm_l15 = t260;
                        }
                    }
gm_lb20:;
                    {
                        gm_trqfvmjtt4sij t261;
                        gm_f_list_epvducg((void *)&t261, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l17 = t261;
                    }
                    uint8_t t262 = gm_l12;
                    if (t262) {
                        gm_b16_8 t263 = gm_l11;
                        gm_l18 = t263;
                        gm_l19 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t264 = gm_l19;
                                uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                uint8_t t266 = (t264 < t265);
                                if (!t266) {
                                    goto gm_lb25;
                                }
                                {
                                    void * t267 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                    uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint64_t t269 = gm_l19;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t267) + (uintptr_t)t269 * 136), 136);
                                            uint8_t t270 = (uint8_t)((((uint8_t *)&F.l20))[32] != 0);
                                            if (t270) {
                                                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&F.l20), 32);
                                            } else {
                                                void * t271 = gm_l0;
                                                void * t272 = gm_l2;
                                                gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t272) + 72));
                                                gm_b16_8 t274;
                                                GM_ST(void *, (uint8_t *)&t274, (void *)(gm_sjdn5gs7lu39i));
                                                GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(56));
                                                gm_b0_1 t275 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t271, t273, t274);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t276 = gm_l3;
                                                gm_t88rcucg1b7uk t277 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t276);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t277;
                                                goto gm_fret;
                                            }
                                            void * t278 = (void *)((uint8_t *)&F.l17);
                                            gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                            gm_tftb1odc739t5 t280 = gm_f_push_3ef746b(t278, t279);
                                            gm_l99 = t280;
                                            uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                                            if (!t281) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srcsffver18si54, 372, 21);
                                            goto gm_lb28;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 367, 17);
                                    }
gm_lb28:;
                                }
gm_lb27:;
                                uint64_t t282 = gm_l19;
                                uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                                uint64_t t285 = r283;
                                gm_l19 = t285;
                            }
                        }
gm_lb25:;
                        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                        gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                        gm_l22 = t287;
                        gm_l23 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t288 = gm_l23;
                                uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                uint8_t t290 = (t288 < t289);
                                if (!t290) {
                                    goto gm_lb30;
                                }
                                {
                                    uint64_t t291 = gm_l23;
                                    void * t292 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                    uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                    uint64_t t294 = gm_l23;
                                    gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)t292) + (uintptr_t)t294 * 16));
                                    GM_ST(uint64_t, ((uint8_t *)&gm_l100 + 0), t291);
                                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l100 + 8), t295);
                                    {
                                        {
                                            uint64_t t296 = GM_LD(uint64_t, ((uint8_t *)&gm_l100));
                                            gm_l24 = t296;
                                            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                                            gm_l25 = t297;
                                            gm_b16_8 t298 = gm_l25;
                                            void * t299 = GM_LD(void *, ((uint8_t *)&F.l17));
                                            uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                            uint64_t t301 = gm_l24;
                                            uint64_t t302 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t303 = (uintptr_t)t301;
                                            if (t302 > t303 || t303 > (uintptr_t)t300) gm_trap_slice(t302, t303, t300, gm_srcsffver18si54, 375, 29);
                                            uint64_t t304 = ((uintptr_t)t301) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t305;
                                            GM_ST(void *, (uint8_t *)&t305, (void *)(((uint8_t *)t299 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                            GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(t304));
                                            uint8_t t306 = 0;
                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t305) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t305))) + i * 16, ((uint8_t *)&t298))) { t306 = 1; break; }
                                            uint8_t t307 = (uint8_t)(t306);
                                            if (t307) {
                                                void * t308 = gm_l0;
                                                void * t309 = gm_l2;
                                                gm_tl874t2atnlh3 t310 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t309) + 72));
                                                gm_b16_8 t311 = gm_l25;
                                                gm_tnpifv20irnph t312;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t312 + 0), t311);
                                                gm_f_format_6nmb1bf((void *)&gm_l101, (void *)NULL, cx, NULL, t312);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                                if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcsffver18si54, 376, 41);
                                                gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                                                gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                                                gm_b0_1 t316 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t308, t310, t315);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t317 = gm_l3;
                                                gm_t88rcucg1b7uk t318 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t317);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t318;
                                                goto gm_fret;
                                            }
                                            goto gm_lb33;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 374, 17);
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                uint64_t t319 = gm_l23;
                                uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                                uint64_t t322 = r320;
                                gm_l23 = t322;
                            }
                        }
gm_lb30:;
                    }
                    {
                        void * t323 = gm_l3;
                        gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                        gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
                        gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                        gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                        gm_t6j6smj7qoctu t328;
                        GM_ST(gm_b16_8, ((uint8_t *)&t328 + 0), t325);
                        GM_ST(gm_b16_8, ((uint8_t *)&t328 + 16), t327);
                        gm_t53at80pfdus3 t329;
                        GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t329 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t329 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)12ULL));
                        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t329 + 8), t328);
                        gm_t88rcucg1b7uk t330 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t323, &t329);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t330;
                        goto gm_fret;
                    }
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t331 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t332 = (uint8_t)(t331 == ((uint8_t)24ULL));
                    if (!t332) {
                        goto gm_lb35;
                    }
                    void * t333 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l26 = t333;
                    void * t334 = gm_l3;
                    void * t335 = gm_l0;
                    uint64_t t336 = gm_l1;
                    void * t337 = gm_l26;
                    gm_t88rcucg1b7uk t338 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t335, t336, t337);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t339 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t334, t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t339;
                    goto gm_fret;
                }
gm_lb35:;
                {
                    uint8_t t340 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t341 = (uint8_t)(t340 == ((uint8_t)25ULL));
                    if (!t341) {
                        goto gm_lb36;
                    }
                    uint8_t t342 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l27 = t342;
                    void * t343 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l28 = t343;
                    {
                        {
                            void * t344 = gm_l28;
                            uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)t344) + 8) + 0));
                            uint8_t t346 = (uint8_t)(t345 == ((uint8_t)30ULL));
                            if (!t346) {
                                goto gm_lb38;
                            }
                            void * t347 = GM_LD(void *, ((((uint8_t *)t344) + 8) + 8));
                            gm_l29 = t347;
                            uint8_t t348 = gm_l27;
                            if (t348) {
                                void * t349 = gm_l0;
                                void * t350 = gm_l2;
                                gm_tl874t2atnlh3 t351 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t350) + 72));
                                gm_b16_8 t352;
                                GM_ST(void *, (uint8_t *)&t352, (void *)(gm_sgva3apqq9jo2));
                                GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(67));
                                gm_b0_1 t353 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t349, t351, t352);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                void * t354 = gm_l0;
                                uint64_t t355 = gm_l1;
                                void * t356 = gm_l29;
                                gm_b16_8 t357 = gm_f_empty_slice_3jt8vgk();
                                gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
                                gm_f_resolve_fn_sig_pamuhve((void *)&gm_l102, (void *)NULL, cx, NULL, t354, t355, t356, ((uint8_t)0), t358);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t359 = (uint8_t)((((uint8_t *)&gm_l102))[136] != 0);
                            if (t359) {
                                memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l102), 136);
                            } else {
                                void * t360 = gm_l3;
                                gm_t88rcucg1b7uk t361 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t360);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t361;
                                goto gm_fret;
                            }
                            {
                                void * t362 = gm_l3;
                                void * t363 = gm_l0;
                                void * t364 = (void *)((uint8_t *)&F.l30);
                                gm_tddtjfggdat0d t365;
                                gm_f_fn_ty_qaih7ts((void *)&t365, (void *)NULL, cx, NULL, t363, t364);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t53at80pfdus3 t366;
                                GM_ST(uint8_t, (uint8_t *)&t366 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t366 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t366 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t366 + 0), ((uint8_t)16ULL));
                                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t366 + 8), t365);
                                gm_t88rcucg1b7uk t367 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t362, &t366);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t367;
                                goto gm_fret;
                            }
                        }
gm_lb38:;
                    }
gm_lb37:;
                    void * t368 = gm_l3;
                    uint8_t t369 = gm_l27;
                    void * t370 = gm_l0;
                    uint64_t t371 = gm_l1;
                    void * t372 = gm_l28;
                    gm_t88rcucg1b7uk t373 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t370, t371, t372);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t374 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t368, t369, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t374;
                    goto gm_fret;
                }
gm_lb36:;
            }
            {
                {
                    uint8_t t375 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t376 = (uint8_t)(t375 == ((uint8_t)26ULL));
                    if (!t376) {
                        goto gm_lb39;
                    }
                    uint8_t t377 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l31 = t377;
                    void * t378 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l32 = t378;
                    void * t379 = gm_l3;
                    uint8_t t380 = gm_l31;
                    void * t381 = gm_l0;
                    uint64_t t382 = gm_l1;
                    void * t383 = gm_l32;
                    gm_t88rcucg1b7uk t384 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t381, t382, t383);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t385 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t379, t380, t384);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t385;
                    goto gm_fret;
                }
gm_lb39:;
                {
                    uint8_t t386 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t387 = (uint8_t)(t386 == ((uint8_t)27ULL));
                    if (!t387) {
                        goto gm_lb40;
                    }
                    uint8_t t388 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 8));
                    gm_l33 = t388;
                    void * t389 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                    gm_l34 = t389;
                    void * t390 = gm_l3;
                    uint8_t t391 = gm_l33;
                    void * t392 = gm_l0;
                    uint64_t t393 = gm_l1;
                    void * t394 = gm_l34;
                    gm_t88rcucg1b7uk t395 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t392, t393, t394);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t396 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t390, t391, t395);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t396;
                    goto gm_fret;
                }
gm_lb40:;
                {
                    uint8_t t397 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t398 = (uint8_t)(t397 == ((uint8_t)28ULL));
                    if (!t398) {
                        goto gm_lb41;
                    }
                    gm_t3mf58gv9kf3n t399 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t137) + 8) + 8));
                    gm_l35 = t399;
                    void * t400 = gm_l0;
                    uint64_t t401 = gm_l1;
                    void * t402 = GM_LD(void *, (((uint8_t *)&gm_l35) + 8));
                    gm_t88rcucg1b7uk t403 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t400, t401, t402);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l36 = t403;
                    uint8_t t404 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l35)) != 0);
                    if (t404) {
                        void * t405 = GM_LD(void *, ((uint8_t *)&gm_l35));
                        gm_l37 = t405;
                    } else {
                        void * t406 = gm_l0;
                        void * t407 = gm_l2;
                        gm_tl874t2atnlh3 t408 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t407) + 72));
                        gm_b16_8 t409;
                        GM_ST(void *, (uint8_t *)&t409, (void *)(gm_sm9funvq1222p));
                        GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(43));
                        gm_b0_1 t410 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t406, t408, t409);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t411 = gm_l3;
                        gm_t88rcucg1b7uk t412 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t411);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t412;
                        goto gm_fret;
                    }
                    void * t413 = gm_l0;
                    uint64_t t414 = gm_l1;
                    void * t415 = gm_l37;
                    uint8_t t416 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t413, t414, t415);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t416) {
                        void * t417 = gm_l0;
                        uint64_t t418 = gm_l1;
                        void * t419 = gm_l37;
                        gm_t88rcucg1b7uk t420 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t417, t418, t419);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l38 = t420;
                        void * t421 = gm_l0;
                        gm_t88rcucg1b7uk t422 = gm_l38;
                        gm_t88rcucg1b7uk t423 = gm_l36;
                        void * t424 = gm_l37;
                        gm_tl874t2atnlh3 t425 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t424) + 72));
                        gm_t88rcucg1b7uk t426 = gm_f_enum_array_type_0sic6t0((void *)NULL, cx, NULL, t421, t422, t423, t425);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t426;
                        goto gm_fret;
                    }
                    void * t427 = gm_l0;
                    uint64_t t428 = gm_l1;
                    void * t429 = gm_l37;
                    gm_t5e6kkuq21nig t430 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t427, t428, t429);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l103 = t430;
                    {
                        {
                            uint8_t t431 = GM_LD(uint8_t, (((uint8_t *)&gm_l103) + 0));
                            uint8_t t432 = (uint8_t)(t431 == ((uint8_t)0ULL));
                            if (!t432) {
                                goto gm_lb43;
                            }
                            uint64_t t433 = GM_LD(uint64_t, (((uint8_t *)&gm_l103) + 8));
                            gm_l39 = t433;
                            void * t434 = gm_l3;
                            uint64_t t435 = gm_l39;
                            gm_t88rcucg1b7uk t436 = gm_l36;
                            gm_t88rcucg1b7uk t437 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t434, t435, t436);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t437;
                            goto gm_fret;
                        }
gm_lb43:;
                        {
                            uint8_t t438 = GM_LD(uint8_t, (((uint8_t *)&gm_l103) + 0));
                            uint8_t t439 = (uint8_t)(t438 == ((uint8_t)1ULL));
                            if (!t439) {
                                goto gm_lb44;
                            }
                            gm_t88rcucg1b7uk t440 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l103) + 8));
                            gm_l40 = t440;
                            void * t441 = gm_l3;
                            gm_t88rcucg1b7uk t442 = gm_l40;
                            gm_t88rcucg1b7uk t443 = gm_l36;
                            gm_tgglga8ro3isc t444;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t444 + 0), t442);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t444 + 4), t443);
                            gm_t53at80pfdus3 t445;
                            memset(&t445, 0, sizeof t445);
                            GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t445 + 8), t444);
                            gm_t88rcucg1b7uk t446 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t441, &t445);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t446;
                            goto gm_fret;
                        }
gm_lb44:;
                        {
                            uint8_t t447 = GM_LD(uint8_t, (((uint8_t *)&gm_l103) + 0));
                            uint8_t t448 = (uint8_t)(t447 == ((uint8_t)2ULL));
                            if (!t448) {
                                goto gm_lb45;
                            }
                            void * t449 = gm_l3;
                            gm_t88rcucg1b7uk t450 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t449);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t450;
                            goto gm_fret;
                        }
gm_lb45:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 404, 13);
                    }
gm_lb42:;
                }
gm_lb41:;
            }
            {
                {
                    {
                        {
                            uint8_t t451 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                            uint8_t t452 = (uint8_t)(t451 == ((uint8_t)29ULL));
                            if (!t452) {
                                goto gm_lb48;
                            }
                            void * t453 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                            gm_l41 = t453;
                            void * t454 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                            gm_l42 = t454;
                            goto gm_lb47;
                        }
gm_lb48:;
                        {
                            uint8_t t455 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                            uint8_t t456 = (uint8_t)(t455 == ((uint8_t)17ULL));
                            if (!t456) {
                                goto gm_lb49;
                            }
                            void * t457 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                            gm_l41 = t457;
                            void * t458 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 8));
                            gm_l42 = t458;
                            goto gm_lb47;
                        }
gm_lb49:;
                        goto gm_lb46;
                    }
gm_lb47:;
                    void * t459 = gm_l0;
                    uint64_t t460 = gm_l1;
                    void * t461 = gm_l41;
                    gm_t88rcucg1b7uk t462 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t459, t460, t461);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l45 = t462;
                    void * t463 = gm_l0;
                    uint64_t t464 = gm_l1;
                    void * t465 = gm_l42;
                    gm_t88rcucg1b7uk t466 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t463, t464, t465);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l46 = t466;
                    void * t467 = gm_l3;
                    gm_t88rcucg1b7uk t468 = gm_l45;
                    gm_t88rcucg1b7uk t469 = gm_l46;
                    gm_t88rcucg1b7uk t470 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t467, t468, t469);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t470;
                    goto gm_fret;
                }
gm_lb46:;
                {
                    uint8_t t471 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t472 = (uint8_t)(t471 == ((uint8_t)30ULL));
                    if (!t472) {
                        goto gm_lb50;
                    }
                    void * t473 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l47 = t473;
                    {
                        void * t474 = gm_l0;
                        uint64_t t475 = gm_l1;
                        void * t476 = gm_l47;
                        gm_b16_8 t477 = gm_f_empty_slice_3jt8vgk();
                        gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)&t477));
                        gm_f_resolve_fn_sig_pamuhve((void *)&gm_l104, (void *)NULL, cx, NULL, t474, t475, t476, ((uint8_t)0), t478);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t479 = (uint8_t)((((uint8_t *)&gm_l104))[136] != 0);
                    if (t479) {
                        memmove(((uint8_t *)&F.l48), ((uint8_t *)&gm_l104), 136);
                    } else {
                        void * t480 = gm_l3;
                        gm_t88rcucg1b7uk t481 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t480);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t481;
                        goto gm_fret;
                    }
                    {
                        void * t482 = gm_l3;
                        void * t483 = gm_l0;
                        void * t484 = (void *)((uint8_t *)&F.l48);
                        gm_tddtjfggdat0d t485;
                        gm_f_fn_ty_qaih7ts((void *)&t485, (void *)NULL, cx, NULL, t483, t484);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t53at80pfdus3 t486;
                        GM_ST(uint8_t, (uint8_t *)&t486 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t486 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t486 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t486 + 0), ((uint8_t)15ULL));
                        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t486 + 8), t485);
                        gm_t88rcucg1b7uk t487 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t482, &t486);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t487;
                        goto gm_fret;
                    }
                }
gm_lb50:;
            }
            {
                {
                    uint8_t t488 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t489 = (uint8_t)(t488 == ((uint8_t)31ULL));
                    if (!t489) {
                        goto gm_lb51;
                    }
                    void * t490 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l49 = t490;
                    void * t491 = gm_l3;
                    void * t492 = gm_l0;
                    uint64_t t493 = gm_l1;
                    void * t494 = gm_l49;
                    gm_t88rcucg1b7uk t495 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t492, t493, t494);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t53at80pfdus3 t496;
                    memset(&t496, 0, sizeof t496);
                    GM_ST(uint8_t, ((uint8_t *)&t496 + 0), ((uint8_t)18ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t496 + 8), t495);
                    gm_t88rcucg1b7uk t497 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t491, &t496);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t497;
                    goto gm_fret;
                }
gm_lb51:;
                {
                    {
                        uint8_t t498 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                        uint8_t t499 = (uint8_t)(t498 == ((uint8_t)19ULL));
                        if (!t499) {
                            goto gm_lb52;
                        }
                        void * t500 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                        gm_l50 = t500;
                        gm_b16_8 t501 = GM_LD(gm_b16_8, (((((uint8_t *)t137) + 8) + 8) + 8));
                        gm_l51 = t501;
                        {
                            {
                                void * t502 = gm_l50;
                                uint8_t t503 = GM_LD(uint8_t, ((((uint8_t *)t502) + 8) + 0));
                                uint8_t t504 = (uint8_t)(t503 == ((uint8_t)22ULL));
                                if (!t504) {
                                    goto gm_lb54;
                                }
                                void * t505 = GM_LD(void *, ((((uint8_t *)t502) + 8) + 8));
                                gm_l52 = t505;
                                void * t506 = gm_l0;
                                uint64_t t507 = gm_l1;
                                void * t508 = gm_l52;
                                uint8_t t509 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t506, t507, t508);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (!t509) {
                                    goto gm_lb54;
                                }
                                void * t510 = gm_l3;
                                gm_t88rcucg1b7uk t511 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t510);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t511;
                                goto gm_fret;
                            }
gm_lb54:;
                        }
gm_lb53:;
                        gm_t9805qtonbsag t512;
                        memset(&t512, 0, sizeof t512);
                        gm_l53 = t512;
                        {
                            {
                                {
                                    void * t513 = gm_l0;
                                    uint64_t t514 = gm_l1;
                                    void * t515 = gm_l50;
                                    gm_f_generic_decl_oir9hm6((void *)&gm_l105, (void *)NULL, cx, NULL, t513, t514, t515, ((uint8_t)1));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t516 = (uint8_t)((((uint8_t *)&gm_l105))[24] != 0);
                                if (!t516) {
                                    goto gm_lb56;
                                }
                                {
                                    memmove(((uint8_t *)&gm_l54), ((uint8_t *)&gm_l105), 24);
                                }
                                {
                                    void * t517 = gm_l0;
                                    uint64_t t518 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                    gm_f_resolve_decl_pus042v((void *)&gm_l106, (void *)NULL, cx, NULL, t517, t518);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    {
                                        uint8_t t519 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                                        uint8_t t520 = (uint8_t)(t519 == ((uint8_t)1ULL));
                                        if (!t520) {
                                            goto gm_lb58;
                                        }
                                        uint32_t t521 = GM_LD(uint32_t, (((uint8_t *)&gm_l106) + 8));
                                        gm_l55 = t521;
                                        void * t522 = gm_l0;
                                        uint64_t t523 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                        gm_tl874t2atnlh3 t524 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l54) + 8));
                                        gm_b0_1 t525 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t522, t523, t524);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint32_t t526 = gm_l55;
                                        gm_t9805qtonbsag t527;
                                        GM_ST(uint8_t, (uint8_t *)&t527 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t527 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t527), t526);
                                        ((uint8_t *)&t527)[4] = 1;
                                        gm_l53 = t527;
                                        goto gm_lb57;
                                    }
gm_lb58:;
                                    {
                                        uint8_t t528 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                                        uint8_t t529 = (uint8_t)(t528 == ((uint8_t)11ULL));
                                        if (!t529) {
                                            goto gm_lb59;
                                        }
                                        void * t530 = gm_l3;
                                        gm_t88rcucg1b7uk t531 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t530);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t531;
                                        goto gm_fret;
                                    }
gm_lb59:;
                                    {
                                        goto gm_lb57;
                                    }
gm_lb60:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 425, 17);
                                }
gm_lb57:;
                                goto gm_lb55;
                            }
gm_lb56:;
                            void * t532 = gm_l50;
                            {
                                {
                                    uint8_t t533 = GM_LD(uint8_t, ((((uint8_t *)t532) + 8) + 0));
                                    uint8_t t534 = (uint8_t)(t533 == ((uint8_t)22ULL));
                                    if (!t534) {
                                        goto gm_lb62;
                                    }
                                    gm_l107 = ((uint8_t)1);
                                    goto gm_lb61;
                                }
gm_lb62:;
                                {
                                    gm_l107 = ((uint8_t)0);
                                    goto gm_lb61;
                                }
gm_lb63:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 433, 23);
                            }
gm_lb61:;
                            uint8_t t535 = gm_l107;
                            if (t535) {
                                void * t536 = gm_l3;
                                gm_t88rcucg1b7uk t537 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t536);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t537;
                                goto gm_fret;
                            }
                        }
gm_lb55:;
                    }
                    {
                        uint8_t t538 = (uint8_t)((((uint8_t *)&gm_l53))[4] != 0);
                        if (t538) {
                            uint32_t t539 = GM_LD(uint32_t, ((uint8_t *)&gm_l53));
                            gm_l57 = t539;
                        } else {
                            {
                                {
                                    void * t540 = gm_l50;
                                    uint8_t t541 = GM_LD(uint8_t, ((((uint8_t *)t540) + 8) + 0));
                                    uint8_t t542 = (uint8_t)(t541 == ((uint8_t)6ULL));
                                    if (!t542) {
                                        goto gm_lb65;
                                    }
                                    memmove(((uint8_t *)&gm_l56), ((((uint8_t *)t540) + 8) + 8), 32);
                                    void * t543 = gm_l0;
                                    uint64_t t544 = gm_l1;
                                    gm_b16_8 t545 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    gm_tf5srh4mp4b3i t546 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t543, t544, t545);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l109 = t546;
                                    {
                                        {
                                            uint8_t t547 = (uint8_t)((((uint8_t *)&gm_l109))[8] != 0);
                                            if (t547) {
                                                goto gm_lb67;
                                            }
                                            gm_l108 = ((uint8_t)1);
                                            goto gm_lb66;
                                        }
gm_lb67:;
                                        {
                                            gm_l108 = ((uint8_t)0);
                                            goto gm_lb66;
                                        }
gm_lb68:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 50);
                                    }
gm_lb66:;
                                    uint8_t t548 = gm_l108;
                                    if (!t548) {
                                        goto gm_lb65;
                                    }
                                    void * t549 = gm_l0;
                                    gm_b16_8 t550 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    gm_tkn42u7na3t81 t551 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t549, t550);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l111 = t551;
                                    {
                                        {
                                            uint8_t t552 = (uint8_t)((((uint8_t *)&gm_l111))[4] != 0);
                                            if (t552) {
                                                goto gm_lb70;
                                            }
                                            gm_l110 = ((uint8_t)1);
                                            goto gm_lb69;
                                        }
gm_lb70:;
                                        {
                                            gm_l110 = ((uint8_t)0);
                                            goto gm_lb69;
                                        }
gm_lb71:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 100);
                                    }
gm_lb69:;
                                    uint8_t t553 = gm_l110;
                                    if (!t553) {
                                        goto gm_lb65;
                                    }
                                    void * t554 = gm_l0;
                                    uint64_t t555 = gm_l1;
                                    gm_b16_8 t556 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                                    void * t557 = gm_l50;
                                    gm_tl874t2atnlh3 t558 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t557) + 72));
                                    gm_b0_1 t559 = gm_f_unknown_type_beat5io((void *)NULL, cx, NULL, t554, t555, t556, t558);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb64;
                                }
gm_lb65:;
                                void * t560 = gm_l0;
                                void * t561 = gm_l2;
                                gm_tl874t2atnlh3 t562 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t561) + 72));
                                gm_b16_8 t563;
                                GM_ST(void *, (uint8_t *)&t563, (void *)(gm_s2328gjukbu28));
                                GM_ST(uintptr_t, (uint8_t *)&t563 + 8, (uintptr_t)(15));
                                gm_b0_1 t564 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t560, t562, t563);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
gm_lb64:;
                            void * t565 = gm_l3;
                            gm_t88rcucg1b7uk t566 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t565);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t566;
                            goto gm_fret;
                        }
                        void * t567 = gm_l0;
                        uint64_t t568 = gm_l1;
                        uint32_t t569 = gm_l57;
                        gm_b16_8 t570 = gm_l51;
                        gm_b16_8 t571 = GM_LD(gm_b16_8, ((uint8_t *)&t570));
                        void * t572 = gm_l2;
                        gm_tl874t2atnlh3 t573 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t572) + 72));
                        gm_f_generic_type_args_5b8bvov((void *)&gm_l112, (void *)NULL, cx, NULL, t567, t568, t569, t571, t573);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint8_t t574 = (uint8_t)((((uint8_t *)&gm_l112))[16] != 0);
                        if (t574) {
                            gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l112));
                            gm_l58 = t575;
                        } else {
                            void * t576 = gm_l3;
                            gm_t88rcucg1b7uk t577 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t576);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t577;
                            goto gm_fret;
                        }
                        void * t578 = gm_l0;
                        void * t579 = GM_LD(void *, (((uint8_t *)t578) + 1088));
                        uint64_t t580 = GM_LD(uint64_t, ((((uint8_t *)t578) + 1088) + 8));
                        uint32_t t581 = gm_l57;
                        uint64_t t582 = (uint64_t)t581;
                        if ((uintptr_t)t582 >= (uintptr_t)(t580)) gm_trap_index((uintptr_t)t582, t580, gm_srcsffver18si54, 446, 23);
                        gm_b16_8 t583 = GM_LD(gm_b16_8, ((((uint8_t *)t579) + (uintptr_t)t582 * 184) + 120));
                        gm_l59 = t583;
                        gm_b16_8 t584 = gm_l59;
                        gm_b16_8 t585 = GM_LD(gm_b16_8, ((uint8_t *)&t584));
                        gm_l60 = t585;
                        gm_l61 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t586 = gm_l61;
                                uint64_t t587 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                uint8_t t588 = (t586 < t587);
                                if (!t588) {
                                    goto gm_lb72;
                                }
                                {
                                    uint64_t t589 = gm_l61;
                                    void * t590 = GM_LD(void *, ((uint8_t *)&gm_l60));
                                    uint64_t t591 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                    uint64_t t592 = gm_l61;
                                    uint32_t t593 = GM_LD(uint32_t, (((uint8_t *)t590) + (uintptr_t)t592 * 4));
                                    gm_tn68el8d14vku t594;
                                    GM_ST(uint32_t, (uint8_t *)&t594 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t594 + 0), t589);
                                    GM_ST(uint32_t, ((uint8_t *)&t594 + 8), t593);
                                    gm_l113 = t594;
                                    {
                                        {
                                            uint64_t t595 = GM_LD(uint64_t, ((uint8_t *)&gm_l113));
                                            gm_l62 = t595;
                                            uint32_t t596 = GM_LD(uint32_t, (((uint8_t *)&gm_l113) + 8));
                                            gm_l63 = t596;
                                            uint64_t t597 = gm_l62;
                                            uint64_t t598 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                            uint8_t t599 = (t597 >= t598);
                                            if (t599) {
                                                goto gm_lb72;
                                            }
                                            void * t600 = GM_LD(void *, ((uint8_t *)&gm_l58));
                                            uint64_t t601 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                            uint64_t t602 = gm_l62;
                                            gm_t88rcucg1b7uk t603 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t600) + (uintptr_t)t602 * 4));
                                            gm_l64 = t603;
                                            void * t604 = gm_l0;
                                            void * t605 = GM_LD(void *, (((uint8_t *)t604) + 1128));
                                            uint64_t t606 = GM_LD(uint64_t, ((((uint8_t *)t604) + 1128) + 8));
                                            uint32_t t607 = gm_l63;
                                            uint64_t t608 = (uint64_t)t607;
                                            if ((uintptr_t)t608 >= (uintptr_t)(t606)) gm_trap_index((uintptr_t)t608, t606, gm_srcsffver18si54, 450, 26);
                                            gm_b16_8 t609 = GM_LD(gm_b16_8, ((((uint8_t *)t605) + (uintptr_t)t608 * 48) + 8));
                                            gm_l65 = t609;
                                            gm_l66 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t610 = gm_l66;
                                                    uint64_t t611 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                                                    uint8_t t612 = (t610 < t611);
                                                    if (!t612) {
                                                        goto gm_lb77;
                                                    }
                                                    {
                                                        void * t613 = GM_LD(void *, ((uint8_t *)&gm_l65));
                                                        uint64_t t614 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                                                        uint64_t t615 = gm_l66;
                                                        {
                                                            {
                                                                gm_t9qkh0rsooc8d t616 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t613) + (uintptr_t)t615 * 1));
                                                                gm_l67 = t616;
                                                                void * t617 = gm_l0;
                                                                gm_t88rcucg1b7uk t618 = gm_l64;
                                                                gm_t9qkh0rsooc8d t619 = gm_l67;
                                                                uint8_t t620 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t617, t618, t619);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t621 = (uint8_t)!t620;
                                                                if (t621) {
                                                                    {
                                                                        {
                                                                            void * t622 = gm_l0;
                                                                            uint32_t t623 = GM_LD(uint32_t, (((uint8_t *)t622) + 1888));
                                                                            uint8_t t624 = (t623 > ((uint32_t)0ULL));
                                                                            if (!t624) {
                                                                                goto gm_lb83;
                                                                            }
                                                                            void * t625 = gm_l3;
                                                                            gm_t88rcucg1b7uk t626 = gm_l64;
                                                                            uint8_t t627 = gm_f_has_param_u9t0efs(t625, GM_CONV(gm_csguankko4lmhd, t626));
                                                                            if (!t627) {
                                                                                goto gm_lb83;
                                                                            }
                                                                            void * t628 = gm_l0;
                                                                            void * t629 = (void *)(((uint8_t *)t628) + 1896);
                                                                            gm_t88rcucg1b7uk t630 = gm_l64;
                                                                            gm_t9qkh0rsooc8d t631 = gm_l67;
                                                                            uint32_t t632 = gm_l63;
                                                                            uint32_t t633 = gm_l57;
                                                                            void * t634 = gm_l2;
                                                                            gm_tl874t2atnlh3 t635 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t634) + 72));
                                                                            gm_t8er5olt969ip t636;
                                                                            GM_ST(uint8_t, (uint8_t *)&t636 + 5, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&t636 + 6, 0);
                                                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t636 + 0), t630);
                                                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t636 + 4), t631);
                                                                            GM_ST(uint32_t, ((uint8_t *)&t636 + 8), t632);
                                                                            GM_ST(uint32_t, ((uint8_t *)&t636 + 12), t633);
                                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t636 + 16), t635);
                                                                            gm_tftb1odc739t5 t637 = gm_f_push_afk8dfq(t629, &t636);
                                                                            gm_l114 = t637;
                                                                            uint8_t t638 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                                                                            if (!t638) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 1, gm_srcsffver18si54, 455, 29);
                                                                            goto gm_lb82;
                                                                        }
gm_lb83:;
                                                                        void * t639 = gm_l0;
                                                                        void * t640 = gm_l2;
                                                                        gm_tl874t2atnlh3 t641 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t640) + 72));
                                                                        void * t642 = gm_l0;
                                                                        gm_t88rcucg1b7uk t643 = gm_l64;
                                                                        gm_t9qkh0rsooc8d t644 = gm_l67;
                                                                        uint32_t t645 = gm_l63;
                                                                        uint32_t t646 = gm_l57;
                                                                        gm_b16_8 t647 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t642, t643, t644, t645, t646);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_b0_1 t648 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t639, t641, t647);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
gm_lb82:;
                                                                }
                                                                goto gm_lb80;
                                                            }
gm_lb81:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 450, 17);
                                                        }
gm_lb80:;
                                                    }
gm_lb79:;
                                                    uint64_t t649 = gm_l66;
                                                    uint64_t r650; bool o651 = __builtin_add_overflow(t649, ((uint64_t)1ULL), &r650);
                                                    uint64_t t652 = r650;
                                                    gm_l66 = t652;
                                                }
                                            }
gm_lb77:;
                                            goto gm_lb75;
                                        }
gm_lb76:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 447, 13);
                                    }
gm_lb75:;
                                }
gm_lb74:;
                                uint64_t t653 = gm_l61;
                                uint64_t r654; bool o655 = __builtin_add_overflow(t653, ((uint64_t)1ULL), &r654);
                                uint64_t t656 = r654;
                                gm_l61 = t656;
                            }
                        }
gm_lb72:;
                    }
                    {
                        gm_b16_8 t657 = gm_l58;
                        gm_l68 = t657;
                        gm_l69 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t658 = gm_l69;
                                uint64_t t659 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                uint8_t t660 = (t658 < t659);
                                if (!t660) {
                                    goto gm_lb84;
                                }
                                {
                                    void * t661 = GM_LD(void *, ((uint8_t *)&gm_l68));
                                    uint64_t t662 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                    uint64_t t663 = gm_l69;
                                    {
                                        {
                                            gm_t88rcucg1b7uk t664 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t661) + (uintptr_t)t663 * 4));
                                            gm_l70 = t664;
                                            {
                                                {
                                                    void * t665 = gm_l0;
                                                    gm_t88rcucg1b7uk t666 = gm_l70;
                                                    gm_tkn42u7na3t81 t667 = gm_f_second_class_part_k6i54mk(t665, GM_CONV(gm_csguankko4lmhd, t666));
                                                    gm_l115 = t667;
                                                    uint8_t t668 = (uint8_t)((((uint8_t *)&gm_l115))[4] != 0);
                                                    if (!t668) {
                                                        goto gm_lb90;
                                                    }
                                                    gm_t88rcucg1b7uk t669 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l115));
                                                    gm_l71 = t669;
                                                    void * t670 = gm_l0;
                                                    void * t671 = gm_l2;
                                                    gm_tl874t2atnlh3 t672 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t671) + 72));
                                                    void * t673 = gm_l0;
                                                    gm_t88rcucg1b7uk t674 = gm_l70;
                                                    gm_t88rcucg1b7uk t675 = gm_l71;
                                                    gm_b16_8 t676 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t673, t674, t675);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b0_1 t677 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t670, t672, t676);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    goto gm_lb89;
                                                }
gm_lb90:;
                                            }
gm_lb89:;
                                            goto gm_lb87;
                                        }
gm_lb88:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 462, 13);
                                    }
gm_lb87:;
                                }
gm_lb86:;
                                uint64_t t678 = gm_l69;
                                uint64_t r679; bool o680 = __builtin_add_overflow(t678, ((uint64_t)1ULL), &r679);
                                uint64_t t681 = r679;
                                gm_l69 = t681;
                            }
                        }
gm_lb84:;
                        void * t682 = gm_l0;
                        uint32_t t683 = gm_l57;
                        gm_b16_8 t684 = gm_l58;
                        gm_b16_8 t685 = GM_LD(gm_b16_8, ((uint8_t *)&t684));
                        gm_t88rcucg1b7uk t686 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t682, t683, t685);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l72 = t686;
                        void * t687 = gm_l0;
                        void * t688 = gm_l2;
                        gm_tl874t2atnlh3 t689 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t688) + 72));
                        gm_b0_1 t690 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t687, t689);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t691 = gm_l72;
                        F.ret = t691;
                        goto gm_fret;
                    }
                }
gm_lb52:;
            }
            {
                {
                    uint8_t t692 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t693 = (uint8_t)(t692 == ((uint8_t)35ULL));
                    if (!t693) {
                        goto gm_lb91;
                    }
                    gm_b16_8 t694 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 8) + 8));
                    gm_l73 = t694;
                    void * t695 = GM_LD(void *, (((((uint8_t *)t137) + 8) + 8) + 16));
                    gm_l74 = t695;
                    {
                        {
                            void * t696 = gm_l74;
                            uint8_t t697 = GM_LD(uint8_t, ((((uint8_t *)t696) + 8) + 0));
                            uint8_t t698 = (uint8_t)(t697 == ((uint8_t)25ULL));
                            if (!t698) {
                                goto gm_lb93;
                            }
                            void * t699 = GM_LD(void *, (((((uint8_t *)t696) + 8) + 8) + 8));
                            gm_l75 = t699;
                            void * t700 = gm_l75;
                            {
                                {
                                    uint8_t t701 = GM_LD(uint8_t, ((((uint8_t *)t700) + 8) + 0));
                                    uint8_t t702 = (uint8_t)(t701 == ((uint8_t)30ULL));
                                    if (!t702) {
                                        goto gm_lb95;
                                    }
                                    gm_l116 = ((uint8_t)1);
                                    goto gm_lb94;
                                }
gm_lb95:;
                                {
                                    gm_l116 = ((uint8_t)0);
                                    goto gm_lb94;
                                }
gm_lb96:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 470, 49);
                            }
gm_lb94:;
                            uint8_t t703 = gm_l116;
                            if (!t703) {
                                goto gm_lb93;
                            }
                            void * t704 = gm_l0;
                            uint64_t t705 = gm_l1;
                            gm_b16_8 t706 = gm_l73;
                            gm_b16_8 t707 = GM_LD(gm_b16_8, ((uint8_t *)&t706));
                            void * t708 = gm_l74;
                            void * t709 = gm_l2;
                            gm_tl874t2atnlh3 t710 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t709) + 72));
                            gm_t88rcucg1b7uk t711 = gm_f_callconv_fn_ptr_l9kavc3((void *)NULL, cx, NULL, t704, t705, t707, t708, t710);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t711;
                            goto gm_fret;
                        }
gm_lb93:;
                    }
gm_lb92:;
                    gm_b16_8 t712 = gm_l73;
                    gm_l76 = t712;
                    gm_l77 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t713 = gm_l77;
                            uint64_t t714 = GM_LD(uint64_t, (((uint8_t *)&gm_l76) + 8));
                            uint8_t t715 = (t713 < t714);
                            if (!t715) {
                                goto gm_lb97;
                            }
                            {
                                void * t716 = GM_LD(void *, ((uint8_t *)&gm_l76));
                                uint64_t t717 = GM_LD(uint64_t, (((uint8_t *)&gm_l76) + 8));
                                uint64_t t718 = gm_l77;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l78), (((uint8_t *)t716) + (uintptr_t)t718 * 72), 72);
                                        {
                                            {
                                                gm_b16_8 t719 = GM_LD(gm_b16_8, ((uint8_t *)&F.l78));
                                                gm_b16_8 t720;
                                                GM_ST(void *, (uint8_t *)&t720, (void *)(gm_sqes10usq4q5e));
                                                GM_ST(uintptr_t, (uint8_t *)&t720 + 8, (uintptr_t)(3));
                                                uint8_t t721 = (gm_eq0iqiubfm7e(((uint8_t *)&t719), ((uint8_t *)&t720)));
                                                if (!t721) {
                                                    goto gm_lb103;
                                                }
                                                void * t722 = gm_l74;
                                                {
                                                    {
                                                        uint8_t t723 = GM_LD(uint8_t, ((((uint8_t *)t722) + 8) + 0));
                                                        uint8_t t724 = (uint8_t)(t723 == ((uint8_t)28ULL));
                                                        if (!t724) {
                                                            goto gm_lb105;
                                                        }
                                                        gm_l117 = ((uint8_t)1);
                                                        goto gm_lb104;
                                                    }
gm_lb105:;
                                                    {
                                                        uint8_t t725 = GM_LD(uint8_t, ((((uint8_t *)t722) + 8) + 0));
                                                        uint8_t t726 = (uint8_t)(t725 == ((uint8_t)26ULL));
                                                        if (!t726) {
                                                            goto gm_lb106;
                                                        }
                                                        gm_l117 = ((uint8_t)1);
                                                        goto gm_lb104;
                                                    }
gm_lb106:;
                                                    {
                                                        gm_l117 = ((uint8_t)0);
                                                        goto gm_lb104;
                                                    }
gm_lb107:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 474, 45);
                                                }
gm_lb104:;
                                                uint8_t t727 = gm_l117;
                                                if (!t727) {
                                                    goto gm_lb103;
                                                }
                                                uint64_t t728 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
                                                uint8_t t729 = (t728 > ((uint64_t)1ULL));
                                                if (t729) {
                                                    gm_l118 = ((uint8_t)1);
                                                } else {
                                                    void * t730 = GM_LD(void *, ((uint8_t *)&gm_l73));
                                                    uint64_t t731 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t731)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t731, gm_srcsffver18si54, 475, 41);
                                                    {
                                                        {
                                                            uint8_t t732 = (uint8_t)((((((uint8_t *)t730) + (uintptr_t)((uint64_t)0ULL) * 72) + 32))[16] != 0);
                                                            if (t732) {
                                                                goto gm_lb109;
                                                            }
                                                            gm_l119 = ((uint8_t)0);
                                                            goto gm_lb108;
                                                        }
gm_lb109:;
                                                        {
                                                            gm_l119 = ((uint8_t)1);
                                                            goto gm_lb108;
                                                        }
gm_lb110:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 475, 41);
                                                    }
gm_lb108:;
                                                    uint8_t t733 = gm_l119;
                                                    gm_l118 = t733;
                                                }
                                                uint8_t t734 = gm_l118;
                                                if (t734) {
                                                    void * t735 = gm_l0;
                                                    void * t736 = gm_l2;
                                                    gm_tl874t2atnlh3 t737 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t736) + 72));
                                                    gm_b16_8 t738;
                                                    GM_ST(void *, (uint8_t *)&t738, (void *)(gm_sgrpmaenv85i1));
                                                    GM_ST(uintptr_t, (uint8_t *)&t738 + 8, (uintptr_t)(72));
                                                    gm_b0_1 t739 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t735, t737, t738);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t740 = gm_l3;
                                                    gm_t88rcucg1b7uk t741 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t740);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t741;
                                                    goto gm_fret;
                                                }
                                                void * t742 = gm_l0;
                                                uint64_t t743 = gm_l1;
                                                void * t744 = gm_l74;
                                                void * t745 = gm_l2;
                                                gm_tl874t2atnlh3 t746 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t745) + 72));
                                                gm_t88rcucg1b7uk t747 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t742, t743, t744, t746);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.ret = t747;
                                                goto gm_fret;
                                            }
gm_lb103:;
                                        }
gm_lb102:;
                                        goto gm_lb100;
                                    }
gm_lb101:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 473, 13);
                                }
gm_lb100:;
                            }
gm_lb99:;
                            uint64_t t748 = gm_l77;
                            uint64_t r749; bool o750 = __builtin_add_overflow(t748, ((uint64_t)1ULL), &r749);
                            uint64_t t751 = r749;
                            gm_l77 = t751;
                        }
                    }
gm_lb97:;
                    void * t752 = gm_l74;
                    {
                        {
                            uint8_t t753 = GM_LD(uint8_t, ((((uint8_t *)t752) + 8) + 0));
                            uint8_t t754 = (uint8_t)(t753 == ((uint8_t)28ULL));
                            if (!t754) {
                                goto gm_lb112;
                            }
                            gm_l120 = ((uint8_t)1);
                            goto gm_lb111;
                        }
gm_lb112:;
                        {
                            gm_l120 = ((uint8_t)0);
                            goto gm_lb111;
                        }
gm_lb113:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 482, 16);
                    }
gm_lb111:;
                    uint8_t t755 = gm_l120;
                    if (t755) {
                        void * t756 = gm_l0;
                        uint64_t t757 = gm_l1;
                        gm_b16_8 t758 = gm_l73;
                        gm_b16_8 t759 = GM_LD(gm_b16_8, ((uint8_t *)&t758));
                        void * t760 = gm_l74;
                        void * t761 = gm_l2;
                        gm_tl874t2atnlh3 t762 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t761) + 72));
                        gm_t88rcucg1b7uk t763 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t756, t757, t759, t760, t762);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t763;
                        goto gm_fret;
                    }
                    void * t764 = gm_l74;
                    {
                        {
                            uint8_t t765 = GM_LD(uint8_t, ((((uint8_t *)t764) + 8) + 0));
                            uint8_t t766 = (uint8_t)(t765 == ((uint8_t)32ULL));
                            if (!t766) {
                                goto gm_lb115;
                            }
                            gm_l121 = ((uint8_t)0);
                            goto gm_lb114;
                        }
gm_lb115:;
                        {
                            uint8_t t767 = GM_LD(uint8_t, ((((uint8_t *)t764) + 8) + 0));
                            uint8_t t768 = (uint8_t)(t767 == ((uint8_t)33ULL));
                            if (!t768) {
                                goto gm_lb116;
                            }
                            gm_l121 = ((uint8_t)0);
                            goto gm_lb114;
                        }
gm_lb116:;
                        {
                            gm_l121 = ((uint8_t)1);
                            goto gm_lb114;
                        }
gm_lb117:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 483, 16);
                    }
gm_lb114:;
                    uint8_t t769 = gm_l121;
                    if (t769) {
                        void * t770 = gm_l0;
                        void * t771 = gm_l2;
                        gm_tl874t2atnlh3 t772 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t771) + 72));
                        gm_b16_8 t773;
                        GM_ST(void *, (uint8_t *)&t773, (void *)(gm_ssj1a8k9nspq4));
                        GM_ST(uintptr_t, (uint8_t *)&t773 + 8, (uintptr_t)(81));
                        gm_b0_1 t774 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t770, t772, t773);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t775 = gm_l3;
                        gm_t88rcucg1b7uk t776 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t775);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t776;
                        goto gm_fret;
                    }
                    void * t777 = gm_l74;
                    {
                        {
                            uint8_t t778 = GM_LD(uint8_t, ((((uint8_t *)t777) + 8) + 0));
                            uint8_t t779 = (uint8_t)(t778 == ((uint8_t)33ULL));
                            if (!t779) {
                                goto gm_lb119;
                            }
                            gm_l122 = ((uint8_t)1);
                            goto gm_lb118;
                        }
gm_lb119:;
                        {
                            gm_l122 = ((uint8_t)0);
                            goto gm_lb118;
                        }
gm_lb120:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 487, 24);
                    }
gm_lb118:;
                    uint8_t t780 = gm_l122;
                    if (t780) {
                        gm_b16_8 t781;
                        GM_ST(void *, (uint8_t *)&t781, (void *)(gm_s8p1h96ji9kh0));
                        GM_ST(uintptr_t, (uint8_t *)&t781 + 8, (uintptr_t)(5));
                        gm_l79 = t781;
                    } else {
                        gm_b16_8 t782;
                        GM_ST(void *, (uint8_t *)&t782, (void *)(gm_sc49g68j2cekh));
                        GM_ST(uintptr_t, (uint8_t *)&t782 + 8, (uintptr_t)(6));
                        gm_l79 = t782;
                    }
                    void * t783 = gm_l0;
                    uint64_t t784 = gm_l1;
                    gm_tf5srh4mp4b3i t785;
                    memset(&t785, 0, sizeof t785);
                    gm_b16_8 t786 = gm_l79;
                    void * t787 = gm_l2;
                    gm_t88rcucg1b7uk t788 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t783, t784, t785, t786, t787);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t788;
                    goto gm_fret;
                }
gm_lb91:;
            }
            {
                {
                    uint8_t t789 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t790 = (uint8_t)(t789 == ((uint8_t)32ULL));
                    if (!t790) {
                        goto gm_lb121;
                    }
                    void * t791 = gm_l0;
                    uint64_t t792 = gm_l1;
                    gm_tf5srh4mp4b3i t793;
                    memset(&t793, 0, sizeof t793);
                    gm_b16_8 t794;
                    GM_ST(void *, (uint8_t *)&t794, (void *)(gm_sc49g68j2cekh));
                    GM_ST(uintptr_t, (uint8_t *)&t794 + 8, (uintptr_t)(6));
                    void * t795 = gm_l2;
                    gm_t88rcucg1b7uk t796 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t791, t792, t793, t794, t795);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t796;
                    goto gm_fret;
                }
gm_lb121:;
                {
                    uint8_t t797 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t798 = (uint8_t)(t797 == ((uint8_t)33ULL));
                    if (!t798) {
                        goto gm_lb122;
                    }
                    void * t799 = gm_l0;
                    uint64_t t800 = gm_l1;
                    gm_tf5srh4mp4b3i t801;
                    memset(&t801, 0, sizeof t801);
                    gm_b16_8 t802;
                    GM_ST(void *, (uint8_t *)&t802, (void *)(gm_s8p1h96ji9kh0));
                    GM_ST(uintptr_t, (uint8_t *)&t802 + 8, (uintptr_t)(5));
                    void * t803 = gm_l2;
                    gm_t88rcucg1b7uk t804 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t799, t800, t801, t802, t803);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t804;
                    goto gm_fret;
                }
gm_lb122:;
                {
                    uint8_t t805 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t806 = (uint8_t)(t805 == ((uint8_t)49ULL));
                    if (!t806) {
                        goto gm_lb123;
                    }
                    memmove(((uint8_t *)&F.l80), ((((uint8_t *)t137) + 8) + 8), 56);
                    gm_b16_8 t807 = GM_LD(gm_b16_8, ((uint8_t *)&F.l80));
                    gm_b16_8 t808;
                    GM_ST(void *, (uint8_t *)&t808, (void *)(gm_slbgk65sat4bm));
                    GM_ST(uintptr_t, (uint8_t *)&t808 + 8, (uintptr_t)(7));
                    uint8_t t809 = (gm_eq0iqiubfm7e(((uint8_t *)&t807), ((uint8_t *)&t808)));
                    if (t809) {
                        {
                            {
                                uint8_t t810 = (uint8_t)(((((uint8_t *)&F.l80) + 32))[16] != 0);
                                if (!t810) {
                                    goto gm_lb125;
                                }
                                gm_b16_8 t811 = GM_LD(gm_b16_8, (((uint8_t *)&F.l80) + 32));
                                gm_l81 = t811;
                                uint64_t t812 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                uint8_t t813 = ((t812 == ((uint64_t)1ULL)));
                                if (!t813) {
                                    goto gm_lb125;
                                }
                                void * t814 = GM_LD(void *, ((uint8_t *)&gm_l81));
                                uint64_t t815 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t815)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t815, gm_srcsffver18si54, 494, 75);
                                uint8_t t816 = GM_LD(uint8_t, ((((uint8_t *)t814) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t817 = (uint8_t)(t816 == ((uint8_t)0ULL));
                                if (!t817) {
                                    goto gm_lb125;
                                }
                                memmove(((uint8_t *)&F.l82), ((((uint8_t *)t814) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                                void * t818 = gm_l0;
                                uint8_t t819 = (uint8_t)(GM_LD(void *, (((uint8_t *)t818) + 2304)) != 0);
                                if (t819) {
                                    void * t820 = GM_LD(void *, (((uint8_t *)t818) + 2304));
                                    gm_l83 = t820;
                                } else {
                                    void * t821 = gm_l0;
                                    void * t822 = gm_l2;
                                    gm_tl874t2atnlh3 t823 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t822) + 72));
                                    gm_b16_8 t824;
                                    GM_ST(void *, (uint8_t *)&t824, (void *)(gm_smi7k9v6u7cmo));
                                    GM_ST(uintptr_t, (uint8_t *)&t824 + 8, (uintptr_t)(10));
                                    gm_b0_1 t825 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t821, t823, t824);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t826 = gm_l3;
                                    gm_t88rcucg1b7uk t827 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t826);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t827;
                                    goto gm_fret;
                                }
                                void * t828 = gm_l83;
                                void * t829 = gm_l0;
                                uint64_t t830 = gm_l1;
                                void * t831 = (void *)((uint8_t *)&F.l82);
                                void * t832 = t828;
                                gm_t88rcucg1b7uk t833;
                                if (GM_PTR_TAGGED(t832)) {
                                    t833 = GM_CONV(gm_t88rcucg1b7uk, ((gm_csguankko4lmhd (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t832))(t829, t830, t831));
                                } else {
                                    t833 = ((gm_t88rcucg1b7uk (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t832)(NULL, cx, NULL, t829, t830, t831);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t833;
                                goto gm_fret;
                            }
gm_lb125:;
                        }
gm_lb124:;
                        void * t834 = gm_l0;
                        void * t835 = gm_l2;
                        gm_tl874t2atnlh3 t836 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t835) + 72));
                        gm_b16_8 t837;
                        GM_ST(void *, (uint8_t *)&t837, (void *)(gm_sfpopfqbubj89));
                        GM_ST(uintptr_t, (uint8_t *)&t837 + 8, (uintptr_t)(34));
                        gm_b0_1 t838 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t834, t836, t837);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t839 = gm_l3;
                        gm_t88rcucg1b7uk t840 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t839);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t840;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb123:;
            }
            {
                {
                    uint8_t t841 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                    uint8_t t842 = (uint8_t)(t841 == ((uint8_t)22ULL));
                    if (!t842) {
                        goto gm_lb126;
                    }
                    void * t843 = GM_LD(void *, ((((uint8_t *)t137) + 8) + 8));
                    gm_l84 = t843;
                    memmove(((uint8_t *)&gm_l85), (((((uint8_t *)t137) + 8) + 8) + 8), 32);
                    {
                        {
                            void * t844 = gm_l0;
                            uint64_t t845 = gm_l1;
                            void * t846 = gm_l84;
                            gm_tf5srh4mp4b3i t847 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t844, t845, t846);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l123 = t847;
                            uint8_t t848 = (uint8_t)((((uint8_t *)&gm_l123))[8] != 0);
                            if (!t848) {
                                goto gm_lb128;
                            }
                            uint64_t t849 = GM_LD(uint64_t, ((uint8_t *)&gm_l123));
                            gm_l86 = t849;
                            {
                                void * t850 = gm_l0;
                                uint64_t t851 = gm_l86;
                                uint64_t t852 = gm_l1;
                                gm_tf5srh4mp4b3i t853 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t850, t851, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l85)), t852);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l124 = t853;
                            }
                            uint8_t t854 = (uint8_t)((((uint8_t *)&gm_l124))[8] != 0);
                            if (t854) {
                                uint64_t t855 = GM_LD(uint64_t, ((uint8_t *)&gm_l124));
                                gm_l87 = t855;
                            } else {
                                void * t856 = gm_l3;
                                gm_t88rcucg1b7uk t857 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t856);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t857;
                                goto gm_fret;
                            }
                            {
                                void * t858 = gm_l0;
                                uint64_t t859 = gm_l87;
                                gm_f_resolve_decl_pus042v((void *)&gm_l125, (void *)NULL, cx, NULL, t858, t859);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                {
                                    uint8_t t860 = GM_LD(uint8_t, (((uint8_t *)&gm_l125) + 0));
                                    uint8_t t861 = (uint8_t)(t860 == ((uint8_t)0ULL));
                                    if (!t861) {
                                        goto gm_lb130;
                                    }
                                    gm_t88rcucg1b7uk t862 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l125) + 8));
                                    gm_l88 = t862;
                                    gm_t88rcucg1b7uk t863 = gm_l88;
                                    F.ret = t863;
                                    goto gm_fret;
                                }
gm_lb130:;
                                {
                                    uint8_t t864 = GM_LD(uint8_t, (((uint8_t *)&gm_l125) + 0));
                                    uint8_t t865 = (uint8_t)(t864 == ((uint8_t)11ULL));
                                    if (!t865) {
                                        goto gm_lb131;
                                    }
                                    void * t866 = gm_l3;
                                    gm_t88rcucg1b7uk t867 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t866);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t867;
                                    goto gm_fret;
                                }
gm_lb131:;
                                {
                                    void * t868 = gm_l0;
                                    void * t869 = gm_l2;
                                    gm_tl874t2atnlh3 t870 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t869) + 72));
                                    gm_b16_8 t871 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l85));
                                    gm_tnpifv20irnph t872;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t872 + 0), t871);
                                    gm_f_format_sv91363((void *)&gm_l126, (void *)NULL, cx, NULL, t872);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t873 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                                    if (!t873) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l126) + 8, gm_srcsffver18si54, 513, 41);
                                    gm_b16_8 t874 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l126) + 8));
                                    gm_b16_8 t875 = GM_LD(gm_b16_8, ((uint8_t *)&t874));
                                    gm_b0_1 t876 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t868, t870, t875);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t877 = gm_l3;
                                    gm_t88rcucg1b7uk t878 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t877);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t878;
                                    goto gm_fret;
                                }
gm_lb132:;
                                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 509, 17);
                            }
gm_lb129:;
                        }
gm_lb128:;
                    }
gm_lb127:;
                    void * t879 = gm_l0;
                    uint64_t t880 = gm_l1;
                    void * t881 = gm_l84;
                    uint8_t t882 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t879, t880, t881);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t882) {
                        void * t883 = gm_l3;
                        gm_t88rcucg1b7uk t884 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t883);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t884;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb126:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb133:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 332, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t885 = gm_l0;
        void * t886 = gm_l2;
        gm_tl874t2atnlh3 t887 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t886) + 72));
        gm_b16_8 t888;
        GM_ST(void *, (uint8_t *)&t888, (void *)(gm_s2328gjukbu28));
        GM_ST(uintptr_t, (uint8_t *)&t888 + 8, (uintptr_t)(15));
        gm_b0_1 t889 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t885, t887, t888);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t890 = gm_l3;
        gm_t88rcucg1b7uk t891 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t890);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t891;
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
extern __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_restore_scope_edqq1v8(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0) { gm_t66j1im9ip128 gm_r; gm_f_scope_mark_i3b2mvo(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_is_type_pe6ugv2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* inner */
    void * gm_l7; /* inner */
    void * gm_l8; /* callee */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    uint64_t gm_l13; /* m */
    uint64_t gm_l14; /* d */
    GM_STACK_CHECK(&F, 783u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    gm_tf5srh4mp4b3i gm_l18;
    void * t29 = gm_l2;
    {
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)6ULL));
            if (!t31) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t29) + 8) + 8), 32);
            {
                {
                    void * t32 = gm_l0;
                    uint64_t t33 = gm_l1;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_tf5srh4mp4b3i t35 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t32, t33, t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t35;
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                    if (!t36) {
                        goto gm_lb4;
                    }
                    uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                    gm_l5 = t37;
                    void * t38 = gm_l0;
                    uint64_t t39 = gm_l5;
                    uint32_t t40 = gm_l3;
                    uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 155, 86);
                    uint32_t t43 = r41;
                    uint8_t t44 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t38, t39, t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t44;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            uint8_t t46 = gm_f_is_builtin_type_name_v5ttoka(t45);
            F.ret = t46;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)9ULL));
            if (!t48) {
                goto gm_lb5;
            }
            void * t49 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l6 = t49;
            void * t50 = gm_l0;
            uint64_t t51 = gm_l1;
            void * t52 = gm_l6;
            uint32_t t53 = gm_l3;
            uint8_t t54 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t50, t51, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t54;
            goto gm_fret;
        }
gm_lb5:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)35ULL));
            if (!t56) {
                goto gm_lb6;
            }
            void * t57 = GM_LD(void *, (((((uint8_t *)t29) + 8) + 8) + 16));
            gm_l7 = t57;
            void * t58 = gm_l0;
            uint64_t t59 = gm_l1;
            void * t60 = gm_l7;
            uint32_t t61 = gm_l3;
            uint8_t t62 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t62;
            goto gm_fret;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)32ULL));
                    if (!t64) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)33ULL));
                    if (!t66) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)25ULL));
                    if (!t68) {
                        goto gm_lb11;
                    }
                    goto gm_lb8;
                }
gm_lb11:;
                {
                    uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)26ULL));
                    if (!t70) {
                        goto gm_lb12;
                    }
                    goto gm_lb8;
                }
gm_lb12:;
                {
                    uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)27ULL));
                    if (!t72) {
                        goto gm_lb13;
                    }
                    goto gm_lb8;
                }
gm_lb13:;
                {
                    uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)29ULL));
                    if (!t74) {
                        goto gm_lb14;
                    }
                    goto gm_lb8;
                }
gm_lb14:;
                {
                    uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)30ULL));
                    if (!t76) {
                        goto gm_lb15;
                    }
                    goto gm_lb8;
                }
gm_lb15:;
                {
                    uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)31ULL));
                    if (!t78) {
                        goto gm_lb16;
                    }
                    goto gm_lb8;
                }
gm_lb16:;
                goto gm_lb7;
            }
gm_lb8:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb7:;
        {
            {
                {
                    uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)24ULL));
                    if (!t80) {
                        goto gm_lb19;
                    }
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)28ULL));
                    if (!t82) {
                        goto gm_lb20;
                    }
                    goto gm_lb18;
                }
gm_lb20:;
                goto gm_lb17;
            }
gm_lb18:;
            void * t83 = gm_l2;
            uint8_t t84 = gm_f_hoists_13k2a2s(t83);
            uint8_t t85 = (uint8_t)!t84;
            F.ret = t85;
            goto gm_fret;
        }
gm_lb17:;
        {
            uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)19ULL));
            if (!t87) {
                goto gm_lb21;
            }
            void * t88 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l8 = t88;
            void * t89 = gm_l8;
            {
                {
                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t89) + 8) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)6ULL));
                    if (!t91) {
                        goto gm_lb23;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t89) + 8) + 8), 32);
                    void * t92 = gm_l0;
                    uint64_t t93 = gm_l1;
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                    gm_tf5srh4mp4b3i t95 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t92, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t95;
                    uint8_t t96 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                    if (t96) {
                        uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                        gm_l10 = t97;
                    } else {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    void * t98 = gm_l0;
                    uint64_t t99 = gm_l10;
                    uint32_t t100 = gm_l3;
                    uint32_t r101; bool o102 = __builtin_add_overflow(t100, ((uint32_t)1ULL), &r101);
                    if (o102) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 165, 42);
                    uint32_t t103 = r101;
                    uint8_t t104 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t98, t99, t103);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t104;
                    goto gm_fret;
                }
gm_lb23:;
                {
                    uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t89) + 8) + 0));
                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)22ULL));
                    if (!t106) {
                        goto gm_lb24;
                    }
                    void * t107 = gm_l0;
                    uint64_t t108 = gm_l1;
                    void * t109 = gm_l8;
                    uint32_t t110 = gm_l3;
                    uint8_t t111 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t107, t108, t109, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t111;
                    goto gm_fret;
                }
gm_lb24:;
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb25:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 162, 32);
            }
gm_lb22:;
        }
gm_lb21:;
        {
            uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)22ULL));
            if (!t113) {
                goto gm_lb26;
            }
            void * t114 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l11 = t114;
            memmove(((uint8_t *)&gm_l12), (((((uint8_t *)t29) + 8) + 8) + 8), 32);
            void * t115 = gm_l0;
            uint64_t t116 = gm_l1;
            void * t117 = gm_l11;
            uint32_t t118 = gm_l3;
            gm_tf5srh4mp4b3i t119 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t115, t116, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t119;
            uint8_t t120 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (t120) {
                uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                gm_l13 = t121;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t122 = gm_l0;
            void * t123 = GM_LD(void *, ((uint8_t *)t122));
            uint64_t t124 = gm_l13;
            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
            gm_tf5srh4mp4b3i t126 = gm_f_lookup_59juqoo(t123, t124, t125);
            gm_l18 = t126;
            uint8_t t127 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
            if (t127) {
                uint64_t t128 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                gm_l14 = t128;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t129 = gm_l0;
            uint64_t t130 = gm_l14;
            uint64_t t131 = gm_l1;
            uint8_t t132 = gm_f_decl_visible_bd168ti(t129, t130, t131);
            uint8_t t133 = (uint8_t)!t132;
            if (t133) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t134 = gm_l0;
            uint64_t t135 = gm_l14;
            uint32_t t136 = gm_l3;
            uint32_t r137; bool o138 = __builtin_add_overflow(t136, ((uint32_t)1ULL), &r137);
            if (o138) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 175, 38);
            uint32_t t139 = r137;
            uint8_t t140 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t134, t135, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t140;
            goto gm_fret;
        }
gm_lb26:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 153, 5);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_module_vho1001 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint64_t gm_l4; /* d */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l10; /* m */
    void * gm_l11; /* decl */
    uint64_t gm_l12; /* dm */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 1342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint64_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    uint64_t gm_l18;
    gm_tf5srh4mp4b3i gm_l19;
    gm_tagrkbqh054h7 gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    gm_t6qooom8qa2do gm_l23;
    uint32_t t25 = gm_l3;
    uint8_t t26 = (t25 > ((uint32_t)16ULL));
    if (t26) {
        gm_tf5srh4mp4b3i t27;
        memset(&t27, 0, sizeof t27);
        F.ret = t27;
        goto gm_fret;
    }
    gm_l4 = ((uint64_t)0ULL);
    void * t28 = gm_l2;
    {
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)6ULL));
            if (!t30) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t28) + 8) + 8), 32);
            void * t31 = gm_l0;
            uint64_t t32 = gm_l1;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_tf5srh4mp4b3i t34 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t34;
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
            if (t35) {
                uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                gm_l15 = t36;
            } else {
                gm_tf5srh4mp4b3i t37;
                memset(&t37, 0, sizeof t37);
                F.ret = t37;
                goto gm_fret;
            }
            uint64_t t38 = gm_l15;
            gm_l4 = t38;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)22ULL));
            if (!t40) {
                goto gm_lb3;
            }
            void * t41 = GM_LD(void *, ((((uint8_t *)t28) + 8) + 8));
            gm_l6 = t41;
            memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t28) + 8) + 8) + 8), 32);
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            void * t44 = gm_l6;
            uint32_t t45 = gm_l3;
            uint32_t r46; bool o47 = __builtin_add_overflow(t45, ((uint32_t)1ULL), &r46);
            if (o47) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 189, 48);
            uint32_t t48 = r46;
            gm_tf5srh4mp4b3i t49 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t42, t43, t44, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t49;
            uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (t50) {
                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                gm_l8 = t51;
            } else {
                gm_tf5srh4mp4b3i t52;
                memset(&t52, 0, sizeof t52);
                F.ret = t52;
                goto gm_fret;
            }
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, ((uint8_t *)t53));
            uint64_t t55 = gm_l8;
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tf5srh4mp4b3i t57 = gm_f_lookup_59juqoo(t54, t55, t56);
            gm_l19 = t57;
            uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
            if (t58) {
                uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                gm_l18 = t59;
            } else {
                gm_tf5srh4mp4b3i t60;
                memset(&t60, 0, sizeof t60);
                F.ret = t60;
                goto gm_fret;
            }
            uint64_t t61 = gm_l18;
            gm_l4 = t61;
            void * t62 = gm_l0;
            uint64_t t63 = gm_l4;
            uint64_t t64 = gm_l1;
            uint8_t t65 = gm_f_decl_visible_bd168ti(t62, t63, t64);
            uint8_t t66 = (uint8_t)!t65;
            if (t66) {
                gm_tf5srh4mp4b3i t67;
                memset(&t67, 0, sizeof t67);
                F.ret = t67;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tf5srh4mp4b3i t68;
            memset(&t68, 0, sizeof t68);
            F.ret = t68;
            goto gm_fret;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 186, 5);
    }
gm_lb1:;
    void * t69 = gm_l0;
    uint64_t t70 = gm_l4;
    gm_f_state_a5bnsjb((void *)&gm_l20, (void *)NULL, cx, NULL, t69, t70);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)2ULL));
            if (!t72) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l20) + 8), 208);
            {
                {
                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)9ULL));
                    if (!t74) {
                        goto gm_lb8;
                    }
                    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                    gm_l10 = t75;
                    uint64_t t76 = gm_l10;
                    gm_tf5srh4mp4b3i t77;
                    GM_ST(uint8_t, (uint8_t *)&t77 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t77 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t77 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t77), t76);
                    ((uint8_t *)&t77)[8] = 1;
                    F.ret = t77;
                    goto gm_fret;
                }
gm_lb8:;
                gm_tf5srh4mp4b3i t78;
                memset(&t78, 0, sizeof t78);
                F.ret = t78;
                goto gm_fret;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            goto gm_lb5;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 195, 5);
    }
gm_lb5:;
    void * t79 = gm_l0;
    void * t80 = GM_LD(void *, ((uint8_t *)t79));
    void * t81 = GM_LD(void *, (((uint8_t *)t80) + 240));
    uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)t80) + 240) + 8));
    uint64_t t83 = gm_l4;
    if ((uintptr_t)t83 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t83, t82, gm_srcsffver18si54, 202, 14);
    void * t84 = (void *)(((uint8_t *)t81) + (uintptr_t)t83 * 216);
    gm_l11 = t84;
    void * t85 = gm_l11;
    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 32));
    gm_l12 = t86;
    {
        {
            void * t87 = gm_l11;
            uint8_t t88 = GM_LD(uint8_t, (((((uint8_t *)t87) + 40) + 64) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)0ULL));
            if (!t89) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&F.l13), (((((uint8_t *)t87) + 40) + 64) + 8), 64);
            {
                {
                    uint8_t t90 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l13) + 24)) != 0);
                    if (t90) {
                        goto gm_lb13;
                    }
                    gm_l22 = ((uint8_t)0);
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    gm_l22 = ((uint8_t)1);
                    goto gm_lb12;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 205, 8);
            }
gm_lb12:;
            uint8_t t91 = gm_l22;
            if (t91) {
                gm_l21 = ((uint8_t)1);
            } else {
                uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 8) + 8));
                uint8_t t93 = (!(t92 == ((uint64_t)1ULL)));
                gm_l21 = t93;
            }
            uint8_t t94 = gm_l21;
            if (t94) {
                gm_tf5srh4mp4b3i t95;
                memset(&t95, 0, sizeof t95);
                F.ret = t95;
                goto gm_fret;
            }
            void * t96 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            {
                {
                    uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t96) + 8) + 0));
                    uint8_t t98 = (uint8_t)(t97 == ((uint8_t)38ULL));
                    if (!t98) {
                        goto gm_lb16;
                    }
                    {
                        {
                            void * t99 = gm_l0;
                            uint64_t t100 = gm_l4;
                            gm_f_resolve_decl_pus042v((void *)&gm_l23, (void *)NULL, cx, NULL, t99, t100);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)9ULL));
                            if (!t102) {
                                goto gm_lb18;
                            }
                            uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            gm_l14 = t103;
                            uint64_t t104 = gm_l14;
                            gm_tf5srh4mp4b3i t105;
                            GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t105), t104);
                            ((uint8_t *)&t105)[8] = 1;
                            F.ret = t105;
                            goto gm_fret;
                        }
gm_lb18:;
                        gm_tf5srh4mp4b3i t106;
                        memset(&t106, 0, sizeof t106);
                        F.ret = t106;
                        goto gm_fret;
                    }
gm_lb17:;
                }
gm_lb16:;
                {
                    {
                        {
                            uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t96) + 8) + 0));
                            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)6ULL));
                            if (!t108) {
                                goto gm_lb21;
                            }
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t96) + 8) + 0));
                            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)22ULL));
                            if (!t110) {
                                goto gm_lb22;
                            }
                            goto gm_lb20;
                        }
gm_lb22:;
                        goto gm_lb19;
                    }
gm_lb20:;
                    void * t111 = gm_l0;
                    uint64_t t112 = gm_l12;
                    void * t113 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
                    uint32_t t114 = gm_l3;
                    uint32_t r115; bool o116 = __builtin_add_overflow(t114, ((uint32_t)1ULL), &r115);
                    if (o116) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 212, 68);
                    uint32_t t117 = r115;
                    gm_tf5srh4mp4b3i t118 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t111, t112, t113, t117);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t118;
                    goto gm_fret;
                }
gm_lb19:;
                {
                    gm_tf5srh4mp4b3i t119;
                    memset(&t119, 0, sizeof t119);
                    F.ret = t119;
                    goto gm_fret;
                }
gm_lb23:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 206, 5);
            }
gm_lb15:;
        }
gm_lb11:;
        gm_tf5srh4mp4b3i t120;
        memset(&t120, 0, sizeof t120);
        F.ret = t120;
        goto gm_fret;
    }
gm_lb10:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_param_3av4r8i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1608) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 55, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1608));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1608) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 56, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1608));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1608) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 56, 57);
                    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_tkn42u7na3t81 t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tkn42u7na3t81 t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unknown_type_beat5io(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_unknown_type_beat5io F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l6; /* hint */
    gm_b16_8 gm_l8; /* last */
    GM_STACK_CHECK(&F, 850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_tqetrgo0q24b5 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_b16_8 t6 = gm_l2;
    uint8_t t7 = gm_f_is_oversized_int_name_nr25haf(t6);
    if (t7) {
        gm_b16_8 t8 = gm_l2;
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_oq3rvor((void *)&gm_l9, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcsffver18si54, 932, 9);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l4 = t11;
    } else {
        gm_b16_8 t12 = gm_l2;
        gm_tnpifv20irnph t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
        gm_f_format_pplcoh0((void *)&gm_l10, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcsffver18si54, 934, 9);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
        gm_l4 = t15;
    }
    {
        gm_tl874t2atnlh3 t16 = gm_l3;
        gm_b16_8 t17 = gm_l4;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_t4gut2c9fj1kt t19;
        gm_f_error_s6hn871((void *)&t19, (void *)NULL, cx, NULL, t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t19;
    }
    {
        {
            void * t20 = gm_l0;
            gm_b16_8 t21 = gm_l2;
            gm_f_universe_hint_ams3f10((void *)&gm_l11, (void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (!t22) {
                goto gm_lb2;
            }
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l6 = t23;
            void * t24 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t25 = gm_l6;
            gm_f_add_note_46v502s(t24, t25);
            {
                {
                    {
                        void * t26 = gm_l0;
                        uint64_t t27 = gm_l1;
                        gm_b16_8 t28 = gm_l2;
                        gm_tl874t2atnlh3 t29 = gm_l3;
                        gm_f_import_fix_7822666((void *)&gm_l12, (void *)NULL, cx, NULL, t26, t27, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l12))[40] != 0);
                    if (!t30) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l12), 40);
                    }
                    void * t31 = GM_LD(void *, ((uint8_t *)&F.l7));
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                    uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
                    if (o35) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 940, 33);
                    uint64_t t36 = r34;
                    if ((uintptr_t)t36 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t36, t32, gm_srcsffver18si54, 940, 21);
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)t31) + (uintptr_t)t36 * 32) + 16));
                    gm_l8 = t37;
                    {
                        void * t38 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t39 = gm_l8;
                        gm_tnpifv20irnph t40;
                        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
                        gm_f_format_ooibcq2((void *)&gm_l13, (void *)NULL, cx, NULL, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcsffver18si54, 941, 29);
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                        gm_b0_1 t46 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t38, t43, t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
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
    {
        void * t47 = gm_l0;
        gm_b0_1 t48 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t47, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_param_tof1h6t F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 1648) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 128, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1648));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1648) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcsffver18si54, 129, 12);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_b16_8 t18 = gm_l1;
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1648));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1648) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcsffver18si54, 129, 57);
                    uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t21) + (uintptr_t)t23 * 24) + 16));
                    gm_t9805qtonbsag t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[4] = 1;
                    F.ret = t25;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_with_operand_ajbkgld F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* operand */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 621u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tq7s98ihonkju gm_l11;
    gm_tq7s98ihonkju gm_l12;
    void * t9 = gm_l0;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)28ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t12;
            void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
            void * t14 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
            void * t15 = gm_l1;
            void * t16 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t17;
            GM_ST(void *, ((uint8_t *)&t17 + 0), t13);
            GM_ST(void *, ((uint8_t *)&t17 + 8), t16);
            memset(&F.l10, 0, sizeof F.l10);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)28ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&F.l10 + 8), t17);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)24ULL));
            if (!t19) {
                goto gm_lb3;
            }
            void * t20 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l3 = t20;
            void * t21 = gm_l3;
            void * t22 = gm_l1;
            void * t23 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&F.l10, 0, sizeof F.l10);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&F.l10 + 8), t23);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)25ULL));
            if (!t25) {
                goto gm_lb4;
            }
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l4 = t26;
            void * t27 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l5 = t27;
            uint8_t t28 = gm_l4;
            void * t29 = gm_l5;
            void * t30 = gm_l1;
            void * t31 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), t28);
            GM_ST(void *, ((uint8_t *)&t32 + 8), t31);
            memset(&F.l10, 0, sizeof F.l10);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)25ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&F.l10 + 8), t32);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)26ULL));
            if (!t34) {
                goto gm_lb5;
            }
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l6 = t35;
            void * t36 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l7 = t36;
            uint8_t t37 = gm_l6;
            void * t38 = gm_l7;
            void * t39 = gm_l1;
            void * t40 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), t37);
            GM_ST(void *, ((uint8_t *)&t41 + 8), t40);
            memset(&F.l10, 0, sizeof F.l10);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)26ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&F.l10 + 8), t41);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)27ULL));
            if (!t43) {
                goto gm_lb6;
            }
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l8 = t44;
            void * t45 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l9 = t45;
            uint8_t t46 = gm_l8;
            void * t47 = gm_l9;
            void * t48 = gm_l1;
            void * t49 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), t46);
            GM_ST(void *, ((uint8_t *)&t50 + 8), t49);
            memset(&F.l10, 0, sizeof F.l10);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)27ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&F.l10 + 8), t50);
            goto gm_lb1;
        }
gm_lb6:;
        {
            {
                void * t51 = gm_l1;
                gm_tq7s98ihonkju t52 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t51)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks263t40fiu)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t52;
            }
            uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcsffver18si54, 923, 21);
            void * t54 = GM_LD(void *, (((uint8_t *)&gm_l11) + 8));
            F.ret = t54;
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 917, 26);
    }
gm_lb1:;
    {
        void * t55 = gm_l0;
        uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)t55));
        void * t57 = gm_l0;
        gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
        gm_t2gtbh39ptekd t59 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l10));
        gm_tc3oht4ksth9d t60;
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 84, 0);
        GM_ST(uint32_t, ((uint8_t *)&t60 + 0), t56);
        GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t60 + 8), t59);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t60 + 72), t58);
        gm_tq7s98ihonkju t61 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t60, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k027ibn62fv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t61;
    }
    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcsffver18si54, 925, 12);
    void * t63 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
    F.ret = t63;
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

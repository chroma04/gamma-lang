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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csnt0cc832rgst { _Bool f0; uint64_t f1; uint64_t f2; uint64_t f3; uint64_t f4; int64_t f5; } gm_csnt0cc832rgst;
typedef struct gm_csv08vjo2a94sp { uint64_t f0; uint64_t f1; int64_t f2; } gm_csv08vjo2a94sp;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; uint64_t f2; uint64_t f3; uint64_t f4; int64_t f5; } gm_t3072hbjtr5t6;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint64_t f1; int64_t f2; } gm_tcs9tli4q9d6j;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csnt0cc832rgst) == 48 && _Alignof(gm_csnt0cc832rgst) == 8 && offsetof(gm_csnt0cc832rgst, f0) == 0 && offsetof(gm_csnt0cc832rgst, f1) == 8 && offsetof(gm_csnt0cc832rgst, f2) == 16 && offsetof(gm_csnt0cc832rgst, f3) == 24 && offsetof(gm_csnt0cc832rgst, f4) == 32 && offsetof(gm_csnt0cc832rgst, f5) == 40, "layout of `Parts`");
_Static_assert(sizeof(gm_csv08vjo2a94sp) == 24 && _Alignof(gm_csv08vjo2a94sp) == 8 && offsetof(gm_csv08vjo2a94sp, f0) == 0 && offsetof(gm_csv08vjo2a94sp, f1) == 8 && offsetof(gm_csv08vjo2a94sp, f2) == 16, "layout of `Normal`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3072hbjtr5t6) == 48 && _Alignof(gm_t3072hbjtr5t6) == 8 && offsetof(gm_t3072hbjtr5t6, f0) == 0 && offsetof(gm_t3072hbjtr5t6, f1) == 8 && offsetof(gm_t3072hbjtr5t6, f2) == 16 && offsetof(gm_t3072hbjtr5t6, f3) == 24 && offsetof(gm_t3072hbjtr5t6, f4) == 32 && offsetof(gm_t3072hbjtr5t6, f5) == 40, "layout of `Parts`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcs9tli4q9d6j) == 24 && _Alignof(gm_tcs9tli4q9d6j) == 8 && offsetof(gm_tcs9tli4q9d6j, f0) == 0 && offsetof(gm_tcs9tli4q9d6j, f1) == 8 && offsetof(gm_tcs9tli4q9d6j, f2) == 16, "layout of `Normal`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; uint64_t v3; uint64_t v4; uint64_t v5; uint64_t v6; uint64_t v7; uint64_t v8; gm_t9qkh0rsooc8d v9; } f1; } gm_tekvgkj2hms37;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_t2lqu8jsj511d v1; } f1; } gm_t1dp1jo1gr2pm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_tnpifv20irnph v1; } f1; } gm_t24hlg9v4qo9a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; uint32_t v1; } f1; } gm_t9mtkhurfjls7;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; } f1; } gm_tekh1vgsjf5ak;
_Static_assert(sizeof(gm_tekvgkj2hms37) == 16 && _Alignof(gm_tekvgkj2hms37) == 8 && offsetof(gm_tekvgkj2hms37, f0) == 0 && offsetof(gm_tekvgkj2hms37, f1) == 8, "layout of `ParseError`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_b16_8 v1; } f1; } gm_tfe4nnqq2l987;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t1dp1jo1gr2pm) == 24 && _Alignof(gm_t1dp1jo1gr2pm) == 8 && offsetof(gm_t1dp1jo1gr2pm, f0) == 0 && offsetof(gm_t1dp1jo1gr2pm, f1) == 8, "layout of `ParseError!(end: usize, len: usize)`");
_Static_assert(sizeof(gm_t24hlg9v4qo9a) == 24 && _Alignof(gm_t24hlg9v4qo9a) == 8 && offsetof(gm_t24hlg9v4qo9a, f0) == 0 && offsetof(gm_t24hlg9v4qo9a, f1) == 8, "layout of `ParseError!Number`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_t9mtkhurfjls7) == 24 && _Alignof(gm_t9mtkhurfjls7) == 8 && offsetof(gm_t9mtkhurfjls7, f0) == 0 && offsetof(gm_t9mtkhurfjls7, f1) == 8, "layout of `ParseError!u32`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_t8vuock3jnv59 v1; } f1; } gm_te0t4fvim36sr;
_Static_assert(sizeof(gm_tekh1vgsjf5ak) == 24 && _Alignof(gm_tekh1vgsjf5ak) == 8 && offsetof(gm_tekh1vgsjf5ak, f0) == 0 && offsetof(gm_tekh1vgsjf5ak, f1) == 8, "layout of `ParseError!()`");
_Static_assert(sizeof(gm_tfe4nnqq2l987) == 24 && _Alignof(gm_tfe4nnqq2l987) == 8 && offsetof(gm_tfe4nnqq2l987, f0) == 0 && offsetof(gm_tfe4nnqq2l987, f1) == 8, "layout of `ParseError!string`");
typedef struct { gm_b16_8 f0; gm_t8vuock3jnv59 f1; } gm_tfkvqluahdpno;
typedef struct { gm_t8vuock3jnv59 f0; gm_tjt4ka19str13 f1; } gm_thshlh2q7ekfv;
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_te0t4fvim36sr) == 32 && _Alignof(gm_te0t4fvim36sr) == 8 && offsetof(gm_te0t4fvim36sr, f0) == 0 && offsetof(gm_te0t4fvim36sr, f1) == 8, "layout of `ParseError!Value`");
_Static_assert(sizeof(gm_tfkvqluahdpno) == 40 && _Alignof(gm_tfkvqluahdpno) == 8 && offsetof(gm_tfkvqluahdpno, f0) == 0 && offsetof(gm_tfkvqluahdpno, f1) == 16, "layout of `Member`");
_Static_assert(sizeof(gm_thshlh2q7ekfv) == 72 && _Alignof(gm_thshlh2q7ekfv) == 8 && offsetof(gm_thshlh2q7ekfv, f0) == 0 && offsetof(gm_thshlh2q7ekfv, f1) == 24, "layout of `Document`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_thshlh2q7ekfv v1; } f1; } gm_tit4ajsn3pv91;
_Static_assert(sizeof(gm_tit4ajsn3pv91) == 80 && _Alignof(gm_tit4ajsn3pv91) == 8 && offsetof(gm_tit4ajsn3pv91, f0) == 0 && offsetof(gm_tit4ajsn3pv91, f1) == 8, "layout of `ParseError!Document`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_l8j91qd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_digit_at_1oh5ota {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    uint8_t ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_encode_utf8_s8ajso1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_get_fjdb5ou {
    gm_frame h;
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_hex4_23k9kdb {
    gm_frame h;
    gm_t9mtkhurfjls7 ret;
};
struct gm_fr_hex_value_q8sogka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_k7levme {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0202sdh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_9ia3g6h {
    gm_frame h;
    gm_tekh1vgsjf5ak ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_normalize_pcair89 {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    gm_tcs9tli4q9d6j ret;
};
struct gm_fr_parse_array_54l90b7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_document_0bpimpc {
    gm_frame h;
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_invslf8 {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* a */
    gm_tit4ajsn3pv91 ret;
    gm_b0_1 w0;
};
struct gm_fr_parse_number_buivaka {
    gm_frame h;
    gm_t24hlg9v4qo9a ret;
};
struct gm_fr_parse_object_4os6ms3 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* members */
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_string_o9jl88i {
    gm_frame h;
    gm_tfe4nnqq2l987 ret;
};
struct gm_fr_parse_value_cgemdat {
    gm_frame h;
    gm_te0t4fvim36sr ret;
};
struct gm_fr_position_f7cepnk {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_push_0pgacsj_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_m55rf90_2 {
    gm_frame h;
    gm_tfkvqluahdpno l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_string_b2p27kc {
    gm_frame h;
    gm_t1dp1jo1gr2pm ret;
};
struct gm_fr_regrow_ddqa9t6_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_skip_ws_g70e352 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_split_25tpoqf {
    gm_frame h;
    gm_t3072hbjtr5t6 ret;
};
struct gm_fr_to_i64_d1vui09 {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    gm_t913bb735pvt5 ret;
};
struct gm_fr_utf8_len_u8bogbn {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota(gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota_c(gm_b16_8 a0, gm_csnt0cc832rgst a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint64_t gm_f_encode_utf8_s8ajso1(uint32_t a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t3ph3kbiqt1ah gm_f_get_fjdb5ou_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hex4_23k9kdb(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9mtkhurfjls7 gm_f_hex4_23k9kdb_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_hex_value_q8sogka(uint8_t a0);
__attribute__((aligned(4))) void gm_f_into_slice_e14ea2s(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_into_slice_hns5a32(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_literal_9ia3g6h(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tekh1vgsjf5ak gm_f_literal_9ia3g6h_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_normalize_pcair89(void *gm_out, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_csv08vjo2a94sp gm_f_normalize_pcair89_c(gm_b16_8 a0, gm_csnt0cc832rgst a1);
__attribute__((aligned(4))) void gm_f_parse_array_54l90b7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_document_0bpimpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_number_buivaka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_object_4os6ms3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_string_o9jl88i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_value_cgemdat(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_position_f7cepnk(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_read_string_b2p27kc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t1dp1jo1gr2pm gm_f_read_string_b2p27kc_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_skip_ws_g70e352(void * a0);
__attribute__((aligned(4))) void gm_f_split_25tpoqf(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_csnt0cc832rgst gm_f_split_25tpoqf_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_d1vui09(gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint64_t gm_f_utf8_len_u8bogbn(gm_b16_8 a0, uint64_t a1);
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
extern uint8_t gm_kfqhpvvf5lv[40] __attribute__((aligned(8)));
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcge9itd5fie17[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrgc9bkquf6nr[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_l8j91qd F;
    void * gm_l0; /* d */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 24);
    gm_f_deinit_sejdpqv(t3);
    void * t4 = gm_l0;
    gm_t8vuock3jnv59 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)t4), t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_4 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_k7levme F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_4();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ab79bmt_2();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_4 F;
    void * gm_l0; /* l */
    gm_t8vuock3jnv59 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6_4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_m55rf90_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ab79bmt_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ocvi9r0_2(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0_2 F;
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
extern __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota(gm_b16_8 a0, const void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_at_1oh5ota F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint64_t r4; bool o5 = __builtin_sub_overflow(t2, t3, &r4);
    if (o5) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 223, 10);
    uint64_t t6 = r4;
    gm_l3 = t6;
    uint64_t t7 = gm_l2;
    uint64_t t8 = gm_l3;
    uint8_t t9 = (t7 < t8);
    if (t9) {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t13 = gm_l2;
        uint64_t r14; bool o15 = __builtin_add_overflow(t12, t13, &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 224, 25);
        uint64_t t16 = r14;
        if ((uintptr_t)t16 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t16, t11, gm_srcge9itd5fie17, 224, 23);
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t16 * 1));
        F.ret = t17;
        goto gm_fret;
    }
    void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    uint64_t t21 = gm_l2;
    uint64_t t22 = gm_l3;
    uint64_t r23; bool o24 = __builtin_sub_overflow(t21, t22, &r23);
    if (o24) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 225, 30);
    uint64_t t25 = r23;
    uint64_t r26; bool o27 = __builtin_add_overflow(t20, t25, &r26);
    if (o27) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 225, 14);
    uint64_t t28 = r26;
    if ((uintptr_t)t28 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t28, t19, gm_srcge9itd5fie17, 225, 12);
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t28 * 1));
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota_c(gm_b16_8 a0, gm_csnt0cc832rgst a1, uint64_t a2) { return gm_f_digit_at_1oh5ota(a0, &a1, a2); }
__attribute__((aligned(4))) uint64_t gm_f_encode_utf8_s8ajso1(uint32_t a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_encode_utf8_s8ajso1 F;
    uint32_t gm_l0; /* cp */
    gm_b16_8 gm_l1; /* buf */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* len */
    GM_STACK_CHECK_SHALLOW(&F, 573u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t gm_l4;
    uint64_t gm_l5;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint32_t)128ULL));
    if (t3) {
        gm_l3 = ((uint64_t)1ULL);
    } else {
        uint32_t t4 = gm_l0;
        uint8_t t5 = (t4 < ((uint32_t)2048ULL));
        if (t5) {
            gm_l4 = ((uint64_t)2ULL);
        } else {
            uint32_t t6 = gm_l0;
            uint8_t t7 = (t6 < ((uint32_t)65536ULL));
            if (t7) {
                gm_l5 = ((uint64_t)3ULL);
            } else {
                gm_l5 = ((uint64_t)4ULL);
            }
            uint64_t t8 = gm_l5;
            gm_l4 = t8;
        }
        uint64_t t9 = gm_l4;
        gm_l3 = t9;
    }
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
    if (t11) {
        uint64_t t12 = gm_l3;
        F.ret = t12;
        goto gm_fret;
    }
    uint64_t t13 = gm_l3;
    uint8_t t14 = ((t13 == ((uint64_t)1ULL)));
    if (t14) {
        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t17 = gm_l2;
        if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srcge9itd5fie17, 458, 9);
        uint32_t t18 = gm_l0;
        uint8_t t19 = (uint8_t)t18;
        GM_ST(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1), t19);
    } else {
        uint64_t t20 = gm_l3;
        uint8_t t21 = ((t20 == ((uint64_t)2ULL)));
        if (t21) {
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t24 = gm_l2;
            if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcge9itd5fie17, 460, 9);
            uint32_t t25 = gm_l0;
            unsigned __int128 n26 = (unsigned __int128)((uint64_t)6ULL);
            if (n26 >= 32) gm_trap_shift((uint64_t)n26, "u32", gm_srcge9itd5fie17, 460, 27);
            uint32_t t27 = (uint32_t)(t25 >> (int)n26);
            uint32_t t28 = (uint32_t)(((uint32_t)192ULL) | t27);
            uint8_t t29 = (uint8_t)t28;
            GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1), t29);
            void * t30 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t32 = gm_l2;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 461, 13);
            uint64_t t35 = r33;
            if ((uintptr_t)t35 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t35, t31, gm_srcge9itd5fie17, 461, 9);
            uint32_t t36 = gm_l0;
            uint32_t t37 = (uint32_t)(t36 & ((uint32_t)63ULL));
            uint32_t t38 = (uint32_t)(((uint32_t)128ULL) | t37);
            uint8_t t39 = (uint8_t)t38;
            GM_ST(uint8_t, (((uint8_t *)t30) + (uintptr_t)t35 * 1), t39);
        } else {
            uint64_t t40 = gm_l3;
            uint8_t t41 = ((t40 == ((uint64_t)3ULL)));
            if (t41) {
                void * t42 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t44 = gm_l2;
                if ((uintptr_t)t44 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t44, t43, gm_srcge9itd5fie17, 463, 9);
                uint32_t t45 = gm_l0;
                unsigned __int128 n46 = (unsigned __int128)((uint64_t)12ULL);
                if (n46 >= 32) gm_trap_shift((uint64_t)n46, "u32", gm_srcge9itd5fie17, 463, 27);
                uint32_t t47 = (uint32_t)(t45 >> (int)n46);
                uint32_t t48 = (uint32_t)(((uint32_t)224ULL) | t47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, (((uint8_t *)t42) + (uintptr_t)t44 * 1), t49);
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t52 = gm_l2;
                uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                if (o54) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 464, 13);
                uint64_t t55 = r53;
                if ((uintptr_t)t55 >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)t55, t51, gm_srcge9itd5fie17, 464, 9);
                uint32_t t56 = gm_l0;
                unsigned __int128 n57 = (unsigned __int128)((uint64_t)6ULL);
                if (n57 >= 32) gm_trap_shift((uint64_t)n57, "u32", gm_srcge9itd5fie17, 464, 32);
                uint32_t t58 = (uint32_t)(t56 >> (int)n57);
                uint32_t t59 = (uint32_t)(t58 & ((uint32_t)63ULL));
                uint32_t t60 = (uint32_t)(((uint32_t)128ULL) | t59);
                uint8_t t61 = (uint8_t)t60;
                GM_ST(uint8_t, (((uint8_t *)t50) + (uintptr_t)t55 * 1), t61);
                void * t62 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t64 = gm_l2;
                uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)2ULL), &r65);
                if (o66) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 465, 13);
                uint64_t t67 = r65;
                if ((uintptr_t)t67 >= (uintptr_t)(t63)) gm_trap_index((uintptr_t)t67, t63, gm_srcge9itd5fie17, 465, 9);
                uint32_t t68 = gm_l0;
                uint32_t t69 = (uint32_t)(t68 & ((uint32_t)63ULL));
                uint32_t t70 = (uint32_t)(((uint32_t)128ULL) | t69);
                uint8_t t71 = (uint8_t)t70;
                GM_ST(uint8_t, (((uint8_t *)t62) + (uintptr_t)t67 * 1), t71);
            } else {
                void * t72 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t74 = gm_l2;
                if ((uintptr_t)t74 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t74, t73, gm_srcge9itd5fie17, 467, 9);
                uint32_t t75 = gm_l0;
                unsigned __int128 n76 = (unsigned __int128)((uint64_t)18ULL);
                if (n76 >= 32) gm_trap_shift((uint64_t)n76, "u32", gm_srcge9itd5fie17, 467, 27);
                uint32_t t77 = (uint32_t)(t75 >> (int)n76);
                uint32_t t78 = (uint32_t)(((uint32_t)240ULL) | t77);
                uint8_t t79 = (uint8_t)t78;
                GM_ST(uint8_t, (((uint8_t *)t72) + (uintptr_t)t74 * 1), t79);
                void * t80 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t82 = gm_l2;
                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                if (o84) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 468, 13);
                uint64_t t85 = r83;
                if ((uintptr_t)t85 >= (uintptr_t)(t81)) gm_trap_index((uintptr_t)t85, t81, gm_srcge9itd5fie17, 468, 9);
                uint32_t t86 = gm_l0;
                unsigned __int128 n87 = (unsigned __int128)((uint64_t)12ULL);
                if (n87 >= 32) gm_trap_shift((uint64_t)n87, "u32", gm_srcge9itd5fie17, 468, 32);
                uint32_t t88 = (uint32_t)(t86 >> (int)n87);
                uint32_t t89 = (uint32_t)(t88 & ((uint32_t)63ULL));
                uint32_t t90 = (uint32_t)(((uint32_t)128ULL) | t89);
                uint8_t t91 = (uint8_t)t90;
                GM_ST(uint8_t, (((uint8_t *)t80) + (uintptr_t)t85 * 1), t91);
                void * t92 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t94 = gm_l2;
                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)2ULL), &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 469, 13);
                uint64_t t97 = r95;
                if ((uintptr_t)t97 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t97, t93, gm_srcge9itd5fie17, 469, 9);
                uint32_t t98 = gm_l0;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)6ULL);
                if (n99 >= 32) gm_trap_shift((uint64_t)n99, "u32", gm_srcge9itd5fie17, 469, 32);
                uint32_t t100 = (uint32_t)(t98 >> (int)n99);
                uint32_t t101 = (uint32_t)(t100 & ((uint32_t)63ULL));
                uint32_t t102 = (uint32_t)(((uint32_t)128ULL) | t101);
                uint8_t t103 = (uint8_t)t102;
                GM_ST(uint8_t, (((uint8_t *)t92) + (uintptr_t)t97 * 1), t103);
                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t106 = gm_l2;
                uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)3ULL), &r107);
                if (o108) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 470, 13);
                uint64_t t109 = r107;
                if ((uintptr_t)t109 >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)t109, t105, gm_srcge9itd5fie17, 470, 9);
                uint32_t t110 = gm_l0;
                uint32_t t111 = (uint32_t)(t110 & ((uint32_t)63ULL));
                uint32_t t112 = (uint32_t)(((uint32_t)128ULL) | t111);
                uint8_t t113 = (uint8_t)t112;
                GM_ST(uint8_t, (((uint8_t *)t104) + (uintptr_t)t109 * 1), t113);
            }
        }
    }
    uint64_t t114 = gm_l3;
    F.ret = t114;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fjdb5ou F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* key */
    gm_b16_8 gm_l2; /* members */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)5ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t10;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_l3 = t11;
            {
                for (;;) {
                    uint64_t t12 = gm_l3;
                    uint8_t t13 = (t12 > ((uint64_t)0ULL));
                    if (!t13) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t14 = gm_l3;
                        uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
                        if (o16) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 132, 17);
                        uint64_t t17 = r15;
                        gm_l3 = t17;
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t20 = gm_l3;
                        if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_srcge9itd5fie17, 133, 20);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)t20 * 40));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (t23) {
                            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t26 = gm_l3;
                            if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcge9itd5fie17, 133, 51);
                            gm_t8vuock3jnv59 t27 = GM_LD(gm_t8vuock3jnv59, ((((uint8_t *)t24) + (uintptr_t)t26 * 40) + 16));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 28, 0);
                            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out)), t27);
                            ((uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out))[24] = 1;
                            goto gm_fdone;
                        }
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            memset(&(*(gm_t3ph3kbiqt1ah *)gm_out), 0, sizeof (*(gm_t3ph3kbiqt1ah *)gm_out));
            goto gm_fdone;
        }
gm_lb2:;
        {
            memset(&(*(gm_t3ph3kbiqt1ah *)gm_out), 0, sizeof (*(gm_t3ph3kbiqt1ah *)gm_out));
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 128, 5);
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t3ph3kbiqt1ah gm_f_get_fjdb5ou_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1) { gm_t3ph3kbiqt1ah gm_r; gm_f_get_fjdb5ou(&gm_r, &a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_hex4_23k9kdb(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex4_23k9kdb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* at */
    uint32_t gm_l2; /* v */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    uint8_t gm_l6; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint32_t)0ULL);
    gm_l4 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)4ULL);
    {
        for (;;) {
            uint64_t t7 = gm_l4;
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t7 < t8);
            if (!t9) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t10 = gm_l4;
                        gm_l5 = t10;
                        uint64_t t11 = gm_l1;
                        uint64_t t12 = gm_l5;
                        uint64_t r13; bool o14 = __builtin_add_overflow(t11, t12, &r13);
                        if (o14) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 416, 12);
                        uint64_t t15 = r13;
                        void * t16 = gm_l0;
                        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
                        uint8_t t18 = (t15 >= t17);
                        if (t18) {
                            void * t19 = gm_l0;
                            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 8));
                            gm_tekvgkj2hms37 t21;
                            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t20);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8), t21);
                            goto gm_fdone;
                        }
                        void * t22 = gm_l0;
                        void * t23 = GM_LD(void *, ((uint8_t *)t22));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
                        uint64_t t25 = gm_l1;
                        uint64_t t26 = gm_l5;
                        uint64_t r27; bool o28 = __builtin_add_overflow(t25, t26, &r27);
                        if (o28) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 417, 31);
                        uint64_t t29 = r27;
                        if ((uintptr_t)t29 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t29, t24, gm_srcge9itd5fie17, 417, 24);
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t29 * 1));
                        uint8_t t31 = gm_f_hex_value_q8sogka(t30);
                        gm_l6 = t31;
                        uint8_t t32 = gm_l6;
                        uint8_t t33 = ((t32 == ((uint8_t)255ULL)));
                        if (t33) {
                            uint64_t t34 = gm_l1;
                            uint64_t t35 = gm_l5;
                            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
                            if (o37) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 418, 46);
                            uint64_t t38 = r36;
                            gm_tekvgkj2hms37 t39;
                            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)3ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8), t39);
                            goto gm_fdone;
                        }
                        uint32_t t40 = gm_l2;
                        uint32_t r41; bool o42 = __builtin_mul_overflow(t40, ((uint32_t)16ULL), &r41);
                        if (o42) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 419, 13);
                        uint32_t t43 = r41;
                        uint8_t t44 = gm_l6;
                        uint32_t t45 = (uint32_t)t44;
                        uint32_t r46; bool o47 = __builtin_add_overflow(t43, t45, &r46);
                        if (o47) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 419, 13);
                        uint32_t t48 = r46;
                        gm_l2 = t48;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 415, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t49 = gm_l4;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l4 = t52;
        }
    }
gm_lb1:;
    uint32_t t53 = gm_l2;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 0;
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8), t53);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t9mtkhurfjls7 gm_f_hex4_23k9kdb_c(void * a0, uint64_t a1) { gm_t9mtkhurfjls7 gm_r; gm_f_hex4_23k9kdb(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_hex_value_q8sogka(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex_value_q8sogka F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t8 = gm_l0;
            uint8_t t9 = (t8 >= ((uint8_t)48ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint8_t t10 = gm_l0;
            uint8_t t11 = (t10 <= ((uint8_t)57ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = gm_l0;
            uint8_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint8_t)48ULL), &r13);
            if (o14) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 406, 39);
            uint8_t t15 = r13;
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t16 = gm_l0;
            uint8_t t17 = (t16 >= ((uint8_t)97ULL));
            if (!t17) {
                goto gm_lb4;
            }
            uint8_t t18 = gm_l0;
            uint8_t t19 = (t18 <= ((uint8_t)102ULL));
            if (!t19) {
                goto gm_lb4;
            }
            uint8_t t20 = gm_l0;
            uint8_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint8_t)97ULL), &r21);
            if (o22) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 407, 39);
            uint8_t t23 = r21;
            uint8_t r24; bool o25 = __builtin_add_overflow(t23, ((uint8_t)10ULL), &r24);
            if (o25) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 407, 39);
            uint8_t t26 = r24;
            F.ret = t26;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            uint8_t t27 = gm_l0;
            uint8_t t28 = (t27 >= ((uint8_t)65ULL));
            if (!t28) {
                goto gm_lb6;
            }
            uint8_t t29 = gm_l0;
            uint8_t t30 = (t29 <= ((uint8_t)70ULL));
            if (!t30) {
                goto gm_lb6;
            }
            uint8_t t31 = gm_l0;
            uint8_t r32; bool o33 = __builtin_sub_overflow(t31, ((uint8_t)65ULL), &r32);
            if (o33) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 408, 39);
            uint8_t t34 = r32;
            uint8_t r35; bool o36 = __builtin_add_overflow(t34, ((uint8_t)10ULL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 408, 39);
            uint8_t t37 = r35;
            F.ret = t37;
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    F.ret = ((uint8_t)255ULL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_literal_9ia3g6h(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_literal_9ia3g6h F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l2 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = gm_l2;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t11 = gm_l3;
                        gm_l4 = t11;
                        void * t12 = gm_l0;
                        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
                        uint64_t t14 = gm_l4;
                        uint64_t r15; bool o16 = __builtin_add_overflow(t13, t14, &r15);
                        if (o16) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 366, 12);
                        uint64_t t17 = r15;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
                        uint8_t t20 = (t17 >= t19);
                        if (t20) {
                            void * t21 = gm_l0;
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
                            gm_tekvgkj2hms37 t23;
                            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t23 + 8), t22);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8), t23);
                            goto gm_fdone;
                        }
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, ((uint8_t *)t24));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
                        void * t27 = gm_l0;
                        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 16));
                        uint64_t t29 = gm_l4;
                        uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 367, 19);
                        uint64_t t32 = r30;
                        if ((uintptr_t)t32 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t32, t26, gm_srcge9itd5fie17, 367, 12);
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t25) + (uintptr_t)t32 * 1));
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t36 = gm_l4;
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t36 * 1));
                        uint8_t t38 = (!(t33 == t37));
                        if (t38) {
                            void * t39 = gm_l0;
                            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
                            uint64_t t41 = gm_l4;
                            uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
                            if (o43) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 367, 71);
                            uint64_t t44 = r42;
                            gm_tekvgkj2hms37 t45;
                            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t44);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8), t45);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 365, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l3;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l3 = t49;
        }
    }
gm_lb1:;
    void * t50 = gm_l0;
    void * t51 = gm_l0;
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r54; bool o55 = __builtin_add_overflow(t52, t53, &r54);
    if (o55) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 369, 5);
    uint64_t t56 = r54;
    GM_ST(uint64_t, (((uint8_t *)t50) + 16), t56);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8), GM_ZERO(gm_b0_1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tekh1vgsjf5ak gm_f_literal_9ia3g6h_c(void * a0, gm_b16_8 a1) { gm_tekh1vgsjf5ak gm_r; gm_f_literal_9ia3g6h(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_normalize_pcair89(void *gm_out, gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_normalize_pcair89 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l2; /* total */
    uint64_t gm_l3; /* lo */
    uint64_t gm_l4; /* hi */
    int64_t gm_l5; /* int_len */
    GM_STACK_CHECK_SHALLOW(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint64_t r10; bool o11 = __builtin_sub_overflow(t8, t9, &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 229, 15);
    uint64_t t12 = r10;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 32));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    uint64_t r15; bool o16 = __builtin_sub_overflow(t13, t14, &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 229, 43);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t12, t17, &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 229, 14);
    uint64_t t20 = r18;
    gm_l2 = t20;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l3;
            uint64_t t22 = gm_l2;
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            gm_b16_8 t24 = gm_l0;
            uint64_t t25 = gm_l3;
            uint8_t t26 = gm_f_digit_at_1oh5ota(t24, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)a1)), t25);
            uint8_t t27 = ((t26 == ((uint8_t)48ULL)));
            if (!t27) {
                goto gm_lb1;
            }
            {
                uint64_t t28 = gm_l3;
                uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 231, 54);
                uint64_t t31 = r29;
                gm_l3 = t31;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t32 = gm_l3;
    uint64_t t33 = gm_l2;
    uint8_t t34 = ((t32 == t33));
    if (t34) {
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 8), ((uint64_t)0ULL));
        GM_ST(int64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 16), ((int64_t)0LL));
        goto gm_fdone;
    }
    uint64_t t35 = gm_l2;
    gm_l4 = t35;
    {
        for (;;) {
            gm_b16_8 t36 = gm_l0;
            uint64_t t37 = gm_l4;
            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 234, 26);
            uint64_t t40 = r38;
            uint8_t t41 = gm_f_digit_at_1oh5ota(t36, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)a1)), t40);
            uint8_t t42 = ((t41 == ((uint8_t)48ULL)));
            if (!t42) {
                goto gm_lb4;
            }
            {
                uint64_t t43 = gm_l4;
                uint64_t r44; bool o45 = __builtin_sub_overflow(t43, ((uint64_t)1ULL), &r44);
                if (o45) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 234, 43);
                uint64_t t46 = r44;
                gm_l4 = t46;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint64_t r49; bool o50 = __builtin_sub_overflow(t47, t48, &r49);
    if (o50) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 235, 17);
    uint64_t t51 = r49;
    int64_t t52 = (int64_t)t51;
    gm_l5 = t52;
    uint64_t t53 = gm_l3;
    uint64_t t54 = gm_l4;
    uint64_t t55 = gm_l3;
    uint64_t r56; bool o57 = __builtin_sub_overflow(t54, t55, &r56);
    if (o57) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 37);
    uint64_t t58 = r56;
    int64_t t59 = gm_l5;
    int64_t r60; bool o61 = __builtin_sub_overflow(t59, ((int64_t)1LL), &r60);
    if (o61) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 51);
    int64_t t62 = r60;
    uint64_t t63 = gm_l3;
    int64_t t64 = (int64_t)t63;
    int64_t r65; bool o66 = __builtin_sub_overflow(t62, t64, &r65);
    if (o66) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 51);
    int64_t t67 = r65;
    int64_t t68 = GM_LD(int64_t, (((uint8_t *)a1) + 40));
    int64_t r69; bool o70 = __builtin_add_overflow(t67, t68, &r69);
    if (o70) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 236, 51);
    int64_t t71 = r69;
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 0), t53);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 8), t58);
    GM_ST(int64_t, ((uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 16), t71);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csv08vjo2a94sp gm_f_normalize_pcair89_c(gm_b16_8 a0, gm_csnt0cc832rgst a1) { gm_tcs9tli4q9d6j gm_r; gm_f_normalize_pcair89(&gm_r, a0, &a1); return GM_CONV(gm_csv08vjo2a94sp, gm_r); }
__attribute__((aligned(4))) void gm_f_parse_array_54l90b7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_array_54l90b7 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    gm_t8vuock3jnv59 gm_l3; /* v */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 740u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_te0t4fvim36sr gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    uint64_t t7 = gm_l1;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 24));
    uint8_t t10 = (t7 >= t9);
    if (t10) {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 16));
        gm_tekvgkj2hms37 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)8ULL));
        GM_ST(uint64_t, ((uint8_t *)&t13 + 8), t12);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t13);
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
    if (o18) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 553, 5);
    uint64_t t19 = r17;
    GM_ST(uint64_t, (((uint8_t *)t14) + 16), t19);
    {
        gm_trqfvmjtt4sij t20;
        gm_f_list_0202sdh_4((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t20;
    }
    void * t21 = gm_l0;
    gm_f_skip_ws_g70e352(t21);
    {
        {
            void * t22 = gm_l0;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 16));
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (t23 < t25);
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 16));
            if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srcge9itd5fie17, 556, 31);
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t31 * 1));
            uint8_t t33 = ((t32 == ((uint8_t)93ULL)));
            if (!t33) {
                goto gm_lb2;
            }
            void * t34 = gm_l0;
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 16));
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 557, 9);
            uint64_t t39 = r37;
            GM_ST(uint64_t, (((uint8_t *)t34) + 16), t39);
            {
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                gm_t8vuock3jnv59 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)4ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t41);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb3;
            }
            {
                void * t42 = gm_l0;
                gm_f_skip_ws_g70e352(t42);
                {
                    void * t43 = gm_l0;
                    uint64_t t44 = gm_l1;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    if (o46) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 562, 33);
                    uint64_t t47 = r45;
                    gm_f_parse_value_cgemdat((void *)&gm_l5, (void *)NULL, cx, NULL, t43, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (t48) {
                } else {
                    gm_tekvgkj2hms37 t49 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l5) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t49);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l5) + 8), 24);
                }
                {
                    void * t50 = (void *)((uint8_t *)&F.l2);
                    gm_tftb1odc739t5 t51 = gm_f_push_0pgacsj_4(t50, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l3)));
                    gm_l6 = t51;
                }
                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (t52) {
                } else {
                    gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 1));
                    gm_tekvgkj2hms37 t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t54 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)9ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 8), t53);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t54);
                    goto gm_fdone;
                }
                void * t55 = gm_l0;
                gm_f_skip_ws_g70e352(t55);
                void * t56 = gm_l0;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
                void * t58 = gm_l0;
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 8));
                uint8_t t60 = (t57 >= t59);
                if (t60) {
                    void * t61 = gm_l0;
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 8));
                    gm_tekvgkj2hms37 t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t63 + 8), t62);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t63);
                    goto gm_fdone;
                }
                void * t64 = gm_l0;
                void * t65 = GM_LD(void *, ((uint8_t *)t64));
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t64) + 8));
                void * t67 = gm_l0;
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 16));
                if ((uintptr_t)t68 >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)t68, t66, gm_srcge9itd5fie17, 566, 14);
                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t65) + (uintptr_t)t68 * 1));
                gm_l4 = t69;
                uint8_t t70 = gm_l4;
                uint8_t t71 = ((t70 == ((uint8_t)44ULL)));
                if (t71) {
                    void * t72 = gm_l0;
                    void * t73 = gm_l0;
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 16));
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 568, 13);
                    uint64_t t77 = r75;
                    GM_ST(uint64_t, (((uint8_t *)t72) + 16), t77);
                } else {
                    uint8_t t78 = gm_l4;
                    uint8_t t79 = ((t78 == ((uint8_t)93ULL)));
                    if (t79) {
                        void * t80 = gm_l0;
                        void * t81 = gm_l0;
                        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 16));
                        uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                        if (o84) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 570, 13);
                        uint64_t t85 = r83;
                        GM_ST(uint64_t, (((uint8_t *)t80) + 16), t85);
                        goto gm_lb3;
                    } else {
                        void * t86 = gm_l0;
                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 16));
                        gm_tekvgkj2hms37 t88;
                        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t88 + 8), t87);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t88);
                        goto gm_fdone;
                    }
                }
            }
gm_lb5:;
        }
    }
gm_lb3:;
    {
        void * t89 = (void *)((uint8_t *)&F.l2);
        gm_f_into_slice_e14ea2s((void *)&gm_l7, t89);
    }
    uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t90) {
    } else {
        gm_t9qkh0rsooc8d t91 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 8));
        gm_tekvgkj2hms37 t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t92 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)9ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t92 + 8), t91);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t92);
        goto gm_fdone;
    }
    {
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_t8vuock3jnv59 t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)4ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 8), t93);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t94);
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
inline __attribute__((aligned(4))) void gm_f_parse_document_0bpimpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_document_0bpimpc F;
    void * gm_l0; /* p */
    gm_t8vuock3jnv59 gm_l1; /* v */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_te0t4fvim36sr gm_l2;
    void * t2 = gm_l0;
    gm_f_skip_ws_g70e352(t2);
    {
        void * t3 = gm_l0;
        gm_f_parse_value_cgemdat((void *)&gm_l2, (void *)NULL, cx, NULL, t3, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t4) {
    } else {
        gm_tekvgkj2hms37 t5 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l2) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t5);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&gm_l1), (((uint8_t *)&gm_l2) + 8), 24);
    }
    void * t6 = gm_l0;
    gm_f_skip_ws_g70e352(t6);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 16));
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint8_t t11 = (t8 < t10);
    if (t11) {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        gm_tekvgkj2hms37 t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)7ULL));
        GM_ST(uint64_t, ((uint8_t *)&t14 + 8), t13);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t14);
        goto gm_fdone;
    }
    {
        gm_t8vuock3jnv59 t15 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t15);
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
inline __attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_parse_invslf8 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* max_depth */
    gm_tgbgti82mf879 gm_l3; /* p */
    gm_te0t4fvim36sr gm_l4; /* res */
    gm_tekvgkj2hms37 gm_l5; /* e */
    gm_t8vuock3jnv59 gm_l6; /* root */
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tjt4ka19str13 t3;
        gm_f_new_jr5ejs0((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l0;
        uint64_t t5 = gm_l1;
        gm_tgbgti82mf879 t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
        GM_ST(uint64_t, ((uint8_t *)&t6 + 16), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t6 + 24), t5);
        gm_l3 = t6;
    }
    {
        gm_t8vuock3jnv59 t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&gm_l4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l4 + 4, 0);
        ((uint8_t *)&gm_l4)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l4 + 8), t7);
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_t3mf58gv9kf3n t9 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t8));
        gm_t3mf58gv9kf3n t10 = t9;
        gm_node wn11 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t10, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t14 = (void *)((uint8_t *)&gm_l3);
        gm_f_parse_document_0bpimpc((void *)&gm_l4, (void *)NULL, (&wn11), NULL, t14);
        if (gm_unw.kind) {
            goto gm_ll13;
        }
    }
    goto gm_ls15;
gm_ll13:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we12; }
    goto gm_lad;
gm_ls15:;
gm_lb1:;
we12:;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t16) {
        memmove(((uint8_t *)&gm_l6), (((uint8_t *)&gm_l4) + 8), 24);
    } else {
        gm_tekvgkj2hms37 t17 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l4) + 8));
        gm_l5 = t17;
        void * t18 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t18);
        gm_tekvgkj2hms37 t19 = gm_l5;
        memset(&(*(gm_tit4ajsn3pv91 *)gm_out), 0, sizeof (*(gm_tit4ajsn3pv91 *)gm_out));
        ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 8), t19);
        goto gm_fdone;
    }
    {
        gm_t8vuock3jnv59 t20 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l6));
        gm_tjt4ka19str13 t21 = GM_LD(gm_tjt4ka19str13, ((uint8_t *)&F.l2));
        gm_thshlh2q7ekfv t22;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t22 + 0), t20);
        GM_ST(gm_tjt4ka19str13, ((uint8_t *)&t22 + 24), t21);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out))[0] = 0;
        GM_ST(gm_thshlh2q7ekfv, ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 8), t22);
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
__attribute__((aligned(4))) void gm_f_parse_number_buivaka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_number_buivaka F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 1128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t21 = gm_l0;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t21));
    gm_l1 = t22;
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
    gm_l2 = t24;
    uint64_t t25 = gm_l2;
    gm_l3 = t25;
    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t28 = gm_l3;
    if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcge9itd5fie17, 377, 8);
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
    uint8_t t30 = ((t29 == ((uint8_t)45ULL)));
    if (t30) {
        uint64_t t31 = gm_l3;
        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
        if (o33) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 377, 22);
        uint64_t t34 = r32;
        gm_l3 = t34;
    }
    uint64_t t35 = gm_l3;
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t37 = (t35 >= t36);
    if (t37) {
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_tekvgkj2hms37 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t39);
        goto gm_fdone;
    }
    void * t40 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t42 = gm_l3;
    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t40) + (uintptr_t)t42 * 1));
    uint8_t t44 = ((t43 == ((uint8_t)48ULL)));
    if (t44) {
        uint64_t t45 = gm_l3;
        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 380, 9);
        uint64_t t48 = r46;
        gm_l3 = t48;
        {
            {
                uint64_t t49 = gm_l3;
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint8_t t51 = (t49 < t50);
                if (!t51) {
                    goto gm_lb2;
                }
                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t54 = gm_l3;
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t52) + (uintptr_t)t54 * 1));
                uint8_t t56 = gm_f_is_digit_k7levme(t55);
                if (!t56) {
                    goto gm_lb2;
                }
                uint64_t t57 = gm_l3;
                gm_tekvgkj2hms37 t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t58 + 8), t57);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t58);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    } else {
        {
            {
                void * t59 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t61 = gm_l3;
                if ((uintptr_t)t61 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t61, t60, gm_srcge9itd5fie17, 382, 15);
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t59) + (uintptr_t)t61 * 1));
                uint8_t t63 = (t62 >= ((uint8_t)49ULL));
                if (!t63) {
                    goto gm_lb4;
                }
                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t66 = gm_l3;
                if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_srcge9itd5fie17, 382, 31);
                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)t66 * 1));
                uint8_t t68 = (t67 <= ((uint8_t)57ULL));
                if (!t68) {
                    goto gm_lb4;
                }
                {
                    for (;;) {
                        uint64_t t69 = gm_l3;
                        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint8_t t71 = (t69 < t70);
                        if (!t71) {
                            goto gm_lb5;
                        }
                        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t74 = gm_l3;
                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)t72) + (uintptr_t)t74 * 1));
                        uint8_t t76 = gm_f_is_digit_k7levme(t75);
                        if (!t76) {
                            goto gm_lb5;
                        }
                        {
                            uint64_t t77 = gm_l3;
                            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                            if (o79) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 383, 46);
                            uint64_t t80 = r78;
                            gm_l3 = t80;
                        }
gm_lb7:;
                    }
                }
gm_lb5:;
                goto gm_lb3;
            }
gm_lb4:;
            uint64_t t81 = gm_l3;
            gm_tekvgkj2hms37 t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t82 + 8), t81);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t82);
            goto gm_fdone;
        }
gm_lb3:;
    }
    {
        {
            uint64_t t83 = gm_l3;
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t85 = (t83 < t84);
            if (!t85) {
                goto gm_lb9;
            }
            void * t86 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t88 = gm_l3;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)t86) + (uintptr_t)t88 * 1));
            uint8_t t90 = ((t89 == ((uint8_t)46ULL)));
            if (!t90) {
                goto gm_lb9;
            }
            uint64_t t91 = gm_l3;
            uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
            if (o93) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 388, 9);
            uint64_t t94 = r92;
            gm_l3 = t94;
            uint64_t t95 = gm_l3;
            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t97 = (t95 >= t96);
            if (t97) {
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                gm_tekvgkj2hms37 t99;
                GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t99 + 8), t98);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t99);
                goto gm_fdone;
            }
            void * t100 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t102 = gm_l3;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)t100) + (uintptr_t)t102 * 1));
            uint8_t t104 = gm_f_is_digit_k7levme(t103);
            uint8_t t105 = (uint8_t)!t104;
            if (t105) {
                uint64_t t106 = gm_l3;
                gm_tekvgkj2hms37 t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t107 + 8), t106);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t107);
                goto gm_fdone;
            }
            {
                for (;;) {
                    uint64_t t108 = gm_l3;
                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint8_t t110 = (t108 < t109);
                    if (!t110) {
                        goto gm_lb10;
                    }
                    void * t111 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t113 = gm_l3;
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t111) + (uintptr_t)t113 * 1));
                    uint8_t t115 = gm_f_is_digit_k7levme(t114);
                    if (!t115) {
                        goto gm_lb10;
                    }
                    {
                        uint64_t t116 = gm_l3;
                        uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                        if (o118) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 391, 46);
                        uint64_t t119 = r117;
                        gm_l3 = t119;
                    }
gm_lb12:;
                }
            }
gm_lb10:;
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    {
        {
            uint64_t t120 = gm_l3;
            uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t122 = (t120 < t121);
            if (!t122) {
                goto gm_lb14;
            }
            void * t123 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t125 = gm_l3;
            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)t123) + (uintptr_t)t125 * 1));
            uint8_t t127 = ((t126 == ((uint8_t)101ULL)));
            if (t127) {
                gm_l4 = ((uint8_t)1);
            } else {
                void * t128 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t130 = gm_l3;
                uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)t128) + (uintptr_t)t130 * 1));
                uint8_t t132 = ((t131 == ((uint8_t)69ULL)));
                gm_l4 = t132;
            }
            uint8_t t133 = gm_l4;
            if (!t133) {
                goto gm_lb14;
            }
            uint64_t t134 = gm_l3;
            uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
            if (o136) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 394, 9);
            uint64_t t137 = r135;
            gm_l3 = t137;
            {
                {
                    uint64_t t138 = gm_l3;
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint8_t t140 = (t138 < t139);
                    if (!t140) {
                        goto gm_lb16;
                    }
                    void * t141 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t143 = gm_l3;
                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)t141) + (uintptr_t)t143 * 1));
                    uint8_t t145 = ((t144 == ((uint8_t)43ULL)));
                    if (t145) {
                        gm_l5 = ((uint8_t)1);
                    } else {
                        void * t146 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t148 = gm_l3;
                        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)t146) + (uintptr_t)t148 * 1));
                        uint8_t t150 = ((t149 == ((uint8_t)45ULL)));
                        gm_l5 = t150;
                    }
                    uint8_t t151 = gm_l5;
                    if (!t151) {
                        goto gm_lb16;
                    }
                    uint64_t t152 = gm_l3;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    if (o154) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 395, 57);
                    uint64_t t155 = r153;
                    gm_l3 = t155;
                    goto gm_lb15;
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t156 = gm_l3;
            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t158 = (t156 >= t157);
            if (t158) {
                uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                gm_tekvgkj2hms37 t160;
                GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t160 + 8), t159);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t160);
                goto gm_fdone;
            }
            void * t161 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t163 = gm_l3;
            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)t161) + (uintptr_t)t163 * 1));
            uint8_t t165 = gm_f_is_digit_k7levme(t164);
            uint8_t t166 = (uint8_t)!t165;
            if (t166) {
                uint64_t t167 = gm_l3;
                gm_tekvgkj2hms37 t168;
                GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t168 + 8), t167);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t168);
                goto gm_fdone;
            }
            {
                for (;;) {
                    uint64_t t169 = gm_l3;
                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint8_t t171 = (t169 < t170);
                    if (!t171) {
                        goto gm_lb17;
                    }
                    void * t172 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t174 = gm_l3;
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)t172) + (uintptr_t)t174 * 1));
                    uint8_t t176 = gm_f_is_digit_k7levme(t175);
                    if (!t176) {
                        goto gm_lb17;
                    }
                    {
                        uint64_t t177 = gm_l3;
                        uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                        if (o179) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 398, 46);
                        uint64_t t180 = r178;
                        gm_l3 = t180;
                    }
gm_lb19:;
                }
            }
gm_lb17:;
            goto gm_lb13;
        }
gm_lb14:;
    }
gm_lb13:;
    void * t181 = gm_l0;
    uint64_t t182 = gm_l3;
    GM_ST(uint64_t, (((uint8_t *)t181) + 16), t182);
    {
        void * t183 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t185 = gm_l2;
        uint64_t t186 = gm_l3;
        uint64_t t187 = (uintptr_t)t185;
        uint64_t t188 = (uintptr_t)t186;
        if (t187 > t188 || t188 > (uintptr_t)t184) gm_trap_slice(t187, t188, t184, gm_srcge9itd5fie17, 401, 34);
        uint64_t t189 = ((uintptr_t)t186) - (uintptr_t)t185;
        gm_b16_8 t190;
        GM_ST(void *, (uint8_t *)&t190, (void *)(((uint8_t *)t183 + (uintptr_t)t185 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(t189));
        gm_f_dupe_g9iqpvn((void *)&gm_l6, (void *)NULL, cx, NULL, t190, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdjk0ti7atv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t191) {
    } else {
        gm_t9qkh0rsooc8d t192 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
        gm_tekvgkj2hms37 t193;
        GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t193 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)9ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t193 + 8), t192);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t193);
        goto gm_fdone;
    }
    {
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
        gm_tnpifv20irnph t196;
        GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t195);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 0;
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8), t196);
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
__attribute__((aligned(4))) void gm_f_parse_object_4os6ms3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_object_4os6ms3 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* k */
    gm_t8vuock3jnv59 gm_l4; /* v */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 1227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tfe4nnqq2l987 gm_l6;
    gm_te0t4fvim36sr gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    uint64_t t7 = gm_l1;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 24));
    uint8_t t10 = (t7 >= t9);
    if (t10) {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 16));
        gm_tekvgkj2hms37 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)8ULL));
        GM_ST(uint64_t, ((uint8_t *)&t13 + 8), t12);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t13);
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
    if (o18) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 581, 5);
    uint64_t t19 = r17;
    GM_ST(uint64_t, (((uint8_t *)t14) + 16), t19);
    {
        gm_trqfvmjtt4sij t20;
        gm_f_list_792g2go_2((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t20;
    }
    void * t21 = gm_l0;
    gm_f_skip_ws_g70e352(t21);
    {
        {
            void * t22 = gm_l0;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 16));
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (t23 < t25);
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 16));
            if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srcge9itd5fie17, 584, 31);
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t31 * 1));
            uint8_t t33 = ((t32 == ((uint8_t)125ULL)));
            if (!t33) {
                goto gm_lb2;
            }
            void * t34 = gm_l0;
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 16));
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 585, 9);
            uint64_t t39 = r37;
            GM_ST(uint64_t, (((uint8_t *)t34) + 16), t39);
            {
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                gm_t8vuock3jnv59 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t41);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb3;
            }
            {
                void * t42 = gm_l0;
                gm_f_skip_ws_g70e352(t42);
                void * t43 = gm_l0;
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 16));
                void * t45 = gm_l0;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 8));
                uint8_t t47 = (t44 >= t46);
                if (t47) {
                    void * t48 = gm_l0;
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 8));
                    gm_tekvgkj2hms37 t50;
                    GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t50 + 8), t49);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t50);
                    goto gm_fdone;
                }
                void * t51 = gm_l0;
                void * t52 = GM_LD(void *, ((uint8_t *)t51));
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t51) + 8));
                void * t54 = gm_l0;
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 16));
                if ((uintptr_t)t55 >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)t55, t53, gm_srcge9itd5fie17, 591, 12);
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t52) + (uintptr_t)t55 * 1));
                uint8_t t57 = (!(t56 == ((uint8_t)34ULL)));
                if (t57) {
                    void * t58 = gm_l0;
                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 16));
                    gm_tekvgkj2hms37 t60;
                    GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t60 + 8), t59);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t60);
                    goto gm_fdone;
                }
                {
                    void * t61 = gm_l0;
                    gm_f_parse_string_o9jl88i((void *)&gm_l6, (void *)NULL, cx, NULL, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (t62) {
                } else {
                    gm_tekvgkj2hms37 t63 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l6) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t63);
                    goto gm_fdone;
                }
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_l3 = t64;
                void * t65 = gm_l0;
                gm_f_skip_ws_g70e352(t65);
                void * t66 = gm_l0;
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 16));
                void * t68 = gm_l0;
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
                uint8_t t70 = (t67 >= t69);
                if (t70) {
                    void * t71 = gm_l0;
                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
                    gm_tekvgkj2hms37 t73;
                    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t73 + 8), t72);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t73);
                    goto gm_fdone;
                }
                void * t74 = gm_l0;
                void * t75 = GM_LD(void *, ((uint8_t *)t74));
                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t74) + 8));
                void * t77 = gm_l0;
                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 16));
                if ((uintptr_t)t78 >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)t78, t76, gm_srcge9itd5fie17, 595, 12);
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)t78 * 1));
                uint8_t t80 = (!(t79 == ((uint8_t)58ULL)));
                if (t80) {
                    void * t81 = gm_l0;
                    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 16));
                    gm_tekvgkj2hms37 t83;
                    GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t83 + 8), t82);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t83);
                    goto gm_fdone;
                }
                void * t84 = gm_l0;
                void * t85 = gm_l0;
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 16));
                uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
                if (o88) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 596, 9);
                uint64_t t89 = r87;
                GM_ST(uint64_t, (((uint8_t *)t84) + 16), t89);
                void * t90 = gm_l0;
                gm_f_skip_ws_g70e352(t90);
                {
                    void * t91 = gm_l0;
                    uint64_t t92 = gm_l1;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    if (o94) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 598, 33);
                    uint64_t t95 = r93;
                    gm_f_parse_value_cgemdat((void *)&gm_l7, (void *)NULL, cx, NULL, t91, t95);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (t96) {
                } else {
                    gm_tekvgkj2hms37 t97 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l7) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t97);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l7) + 8), 24);
                }
                {
                    void * t98 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t99 = gm_l3;
                    gm_t8vuock3jnv59 t100 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l4));
                    gm_tfkvqluahdpno t101;
                    GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t99);
                    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t101 + 16), t100);
                    gm_tftb1odc739t5 t102 = gm_f_push_m55rf90_2(t98, &t101);
                    gm_l8 = t102;
                }
                uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (t103) {
                } else {
                    gm_t9qkh0rsooc8d t104 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 1));
                    gm_tekvgkj2hms37 t105;
                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)9ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t105 + 8), t104);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t105);
                    goto gm_fdone;
                }
                void * t106 = gm_l0;
                gm_f_skip_ws_g70e352(t106);
                void * t107 = gm_l0;
                uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)t107) + 16));
                void * t109 = gm_l0;
                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)t109) + 8));
                uint8_t t111 = (t108 >= t110);
                if (t111) {
                    void * t112 = gm_l0;
                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t112) + 8));
                    gm_tekvgkj2hms37 t114;
                    GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t114 + 8), t113);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t114);
                    goto gm_fdone;
                }
                void * t115 = gm_l0;
                void * t116 = GM_LD(void *, ((uint8_t *)t115));
                uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)t115) + 8));
                void * t118 = gm_l0;
                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)t118) + 16));
                if ((uintptr_t)t119 >= (uintptr_t)(t117)) gm_trap_index((uintptr_t)t119, t117, gm_srcge9itd5fie17, 602, 14);
                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t116) + (uintptr_t)t119 * 1));
                gm_l5 = t120;
                uint8_t t121 = gm_l5;
                uint8_t t122 = ((t121 == ((uint8_t)44ULL)));
                if (t122) {
                    void * t123 = gm_l0;
                    void * t124 = gm_l0;
                    uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 16));
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    if (o127) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 604, 13);
                    uint64_t t128 = r126;
                    GM_ST(uint64_t, (((uint8_t *)t123) + 16), t128);
                } else {
                    uint8_t t129 = gm_l5;
                    uint8_t t130 = ((t129 == ((uint8_t)125ULL)));
                    if (t130) {
                        void * t131 = gm_l0;
                        void * t132 = gm_l0;
                        uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)t132) + 16));
                        uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                        if (o135) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 606, 13);
                        uint64_t t136 = r134;
                        GM_ST(uint64_t, (((uint8_t *)t131) + 16), t136);
                        goto gm_lb3;
                    } else {
                        void * t137 = gm_l0;
                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)t137) + 16));
                        gm_tekvgkj2hms37 t139;
                        GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t139 + 8), t138);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t139);
                        goto gm_fdone;
                    }
                }
            }
gm_lb5:;
        }
    }
gm_lb3:;
    {
        void * t140 = (void *)((uint8_t *)&F.l2);
        gm_f_into_slice_hns5a32((void *)&gm_l9, t140);
    }
    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t141) {
    } else {
        gm_t9qkh0rsooc8d t142 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l9) + 8));
        gm_tekvgkj2hms37 t143;
        GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t143 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t143 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t143 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)9ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t143 + 8), t142);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t143);
        goto gm_fdone;
    }
    {
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_t8vuock3jnv59 t145;
        GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t145);
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
inline __attribute__((aligned(4))) void gm_f_parse_string_o9jl88i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_string_o9jl88i F;
    void * gm_l0; /* p */
    gm_t2lqu8jsj511d gm_l1; /* r */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t1dp1jo1gr2pm gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_t1dp1jo1gr2pm gm_l5;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_empty_slice_ijtp9lo();
        gm_f_read_string_b2p27kc((void *)&gm_l3, t2, t3);
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t4) {
    } else {
        gm_tekvgkj2hms37 t5 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l3) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8), t5);
        goto gm_fdone;
    }
    gm_t2lqu8jsj511d t6 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&gm_l3) + 8));
    gm_l1 = t6;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
    if (t8) {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
        uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 542, 17);
        uint64_t t13 = r11;
        GM_ST(uint64_t, (((uint8_t *)t9) + 16), t13);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8), t14);
        goto gm_fdone;
    }
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_f_make_2ip78bj((void *)&gm_l4, (void *)NULL, cx, NULL, t15, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kfqhpvvf5lv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t16) {
    } else {
        gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l4) + 8));
        gm_tekvgkj2hms37 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)9ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 8), t17);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8), t18);
        goto gm_fdone;
    }
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_l2 = t19;
    {
        void * t20 = gm_l0;
        gm_b16_8 t21 = gm_l2;
        gm_f_read_string_b2p27kc((void *)&gm_l5, t20, t21);
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t22) {
    } else {
        gm_tekvgkj2hms37 t23 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l5) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8), t23);
        goto gm_fdone;
    }
    gm_t2lqu8jsj511d t24 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&gm_l5) + 8));
    void * t25 = gm_l0;
    uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
    if (o28) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 547, 13);
    uint64_t t29 = r27;
    GM_ST(uint64_t, (((uint8_t *)t25) + 16), t29);
    {
        gm_b16_8 t30 = gm_l2;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8), t31);
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
__attribute__((aligned(4))) void gm_f_parse_value_cgemdat(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_value_cgemdat F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 827u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_te0t4fvim36sr gm_l3;
    gm_tekh1vgsjf5ak gm_l4;
    gm_tekh1vgsjf5ak gm_l5;
    gm_tekh1vgsjf5ak gm_l6;
    gm_tfe4nnqq2l987 gm_l7;
    gm_te0t4fvim36sr gm_l8;
    gm_te0t4fvim36sr gm_l9;
    gm_t24hlg9v4qo9a gm_l10;
    void * t14 = gm_l0;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 16));
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint8_t t18 = (t15 >= t17);
    if (t18) {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 8));
        gm_tekvgkj2hms37 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t20);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
    void * t25 = gm_l0;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 16));
    if ((uintptr_t)t26 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t26, t24, gm_srcge9itd5fie17, 342, 10);
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t26 * 1));
    gm_l2 = t27;
    {
        {
            uint8_t t28 = gm_l2;
            uint8_t t29 = ((t28 == ((uint8_t)110ULL)));
            if (!t29) {
                goto gm_lb2;
            }
            {
                void * t30 = gm_l0;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_squedv51820an));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(4));
                gm_f_literal_9ia3g6h((void *)&gm_l4, t30, t31);
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (t32) {
            } else {
                gm_tekvgkj2hms37 t33 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l4) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t33);
                goto gm_fdone;
            }
            {
                gm_t8vuock3jnv59 t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t34);
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t35 = gm_l2;
            uint8_t t36 = ((t35 == ((uint8_t)116ULL)));
            if (!t36) {
                goto gm_lb3;
            }
            {
                void * t37 = gm_l0;
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sehko5bb96s6p));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
                gm_f_literal_9ia3g6h((void *)&gm_l5, t37, t38);
            }
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t39) {
            } else {
                gm_tekvgkj2hms37 t40 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l5) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t40);
                goto gm_fdone;
            }
            {
                gm_t8vuock3jnv59 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, ((uint8_t *)&t41 + 8), ((uint8_t)1));
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t41);
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t42 = gm_l2;
            uint8_t t43 = ((t42 == ((uint8_t)102ULL)));
            if (!t43) {
                goto gm_lb4;
            }
            {
                void * t44 = gm_l0;
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sogehh8t1m7tn));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(5));
                gm_f_literal_9ia3g6h((void *)&gm_l6, t44, t45);
            }
            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (t46) {
            } else {
                gm_tekvgkj2hms37 t47 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l6) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t47);
                goto gm_fdone;
            }
            {
                gm_t8vuock3jnv59 t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, ((uint8_t *)&t48 + 8), ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t48);
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t49 = gm_l2;
            uint8_t t50 = ((t49 == ((uint8_t)34ULL)));
            if (!t50) {
                goto gm_lb5;
            }
            {
                void * t51 = gm_l0;
                gm_f_parse_string_o9jl88i((void *)&gm_l7, (void *)NULL, cx, NULL, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (t52) {
            } else {
                gm_tekvgkj2hms37 t53 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l7) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t53);
                goto gm_fdone;
            }
            {
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_t8vuock3jnv59 t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)3ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 8), t54);
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t55);
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t56 = gm_l2;
            uint8_t t57 = ((t56 == ((uint8_t)91ULL)));
            if (!t57) {
                goto gm_lb6;
            }
            {
                void * t58 = gm_l0;
                uint64_t t59 = gm_l1;
                gm_f_parse_array_54l90b7((void *)&gm_l8, (void *)NULL, cx, NULL, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (t60) {
            } else {
                gm_tekvgkj2hms37 t61 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l8) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t61);
                goto gm_fdone;
            }
            {
                gm_t8vuock3jnv59 t62 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&gm_l8) + 8));
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t62);
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t63 = gm_l2;
            uint8_t t64 = ((t63 == ((uint8_t)123ULL)));
            if (!t64) {
                goto gm_lb7;
            }
            {
                void * t65 = gm_l0;
                uint64_t t66 = gm_l1;
                gm_f_parse_object_4os6ms3((void *)&gm_l9, (void *)NULL, cx, NULL, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (t67) {
            } else {
                gm_tekvgkj2hms37 t68 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l9) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t68);
                goto gm_fdone;
            }
            {
                gm_t8vuock3jnv59 t69 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&gm_l9) + 8));
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t69);
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            {
                {
                    uint8_t t70 = gm_l2;
                    uint8_t t71 = ((t70 == ((uint8_t)45ULL)));
                    if (!t71) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t72 = gm_l2;
                    uint8_t t73 = (t72 >= ((uint8_t)48ULL));
                    if (!t73) {
                        goto gm_lb11;
                    }
                    uint8_t t74 = (t72 <= ((uint8_t)57ULL));
                    if (!t74) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                goto gm_lb8;
            }
gm_lb9:;
            {
                void * t75 = gm_l0;
                gm_f_parse_number_buivaka((void *)&gm_l10, (void *)NULL, cx, NULL, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (t76) {
            } else {
                gm_tekvgkj2hms37 t77 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l10) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t77);
                goto gm_fdone;
            }
            {
                gm_tnpifv20irnph t78 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l10) + 8));
                gm_t8vuock3jnv59 t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t79 + 8), t78);
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&gm_l3 + 8), t79);
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            void * t80 = gm_l0;
            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 16));
            gm_tekvgkj2hms37 t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t82 + 8), t81);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8), t82);
            goto gm_fdone;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 343, 12);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l3), 32);
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
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_position_f7cepnk(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_position_f7cepnk F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* offset */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t9 = (t7 < t8 ? t7 : t8);
    gm_l2 = t9;
    gm_l3 = ((uint64_t)1ULL);
    gm_l4 = ((uint64_t)0ULL);
    gm_l6 = ((uint64_t)0ULL);
    uint64_t t10 = gm_l2;
    gm_l5 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l6;
            uint64_t t12 = gm_l5;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l6;
                        gm_l7 = t14;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t17 = gm_l7;
                        if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srcge9itd5fie17, 113, 12);
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                        uint8_t t19 = ((t18 == ((uint8_t)10ULL)));
                        if (t19) {
                            uint64_t t20 = gm_l3;
                            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                            if (o22) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 114, 13);
                            uint64_t t23 = r21;
                            gm_l3 = t23;
                            uint64_t t24 = gm_l7;
                            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                            if (o26) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 115, 21);
                            uint64_t t27 = r25;
                            gm_l4 = t27;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 112, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l6;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l6 = t31;
        }
    }
gm_lb1:;
    uint64_t t32 = gm_l3;
    uint64_t t33 = gm_l2;
    uint64_t t34 = gm_l4;
    uint64_t r35; bool o36 = __builtin_sub_overflow(t33, t34, &r35);
    if (o36) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 118, 41);
    uint64_t t37 = r35;
    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
    if (o39) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 118, 41);
    uint64_t t40 = r38;
    gm_t2lqu8jsj511d t41;
    GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t32);
    GM_ST(uint64_t, ((uint8_t *)&t41 + 8), t40);
    F.ret = t41;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) void gm_f_read_string_b2p27kc(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_string_b2p27kc F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* buf */
    gm_b16_8 gm_l2; /* t */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* n */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* e */
    uint8_t gm_l7; /* single */
    uint32_t gm_l8; /* cp */
    uint64_t gm_l9; /* used */
    uint32_t gm_l10; /* lo */
    uint64_t gm_l11; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 1268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9mtkhurfjls7 gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_t9mtkhurfjls7 gm_l15;
    uint8_t gm_l16;
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
        gm_l2 = t20;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 16));
        uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 480, 10);
        uint64_t t25 = r23;
        gm_l3 = t25;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    if (!((uint8_t)1)) {
                        goto gm_lb1;
                    }
                    {
                        {
                            uint64_t t26 = gm_l3;
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint8_t t28 = (t26 >= t27);
                            if (t28) {
                                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                gm_tekvgkj2hms37 t30;
                                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t30 + 8), t29);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t30);
                                goto gm_fdone;
                            }
                            void * t31 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t33 = gm_l3;
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t31) + (uintptr_t)t33 * 1));
                            gm_l5 = t34;
                            uint8_t t35 = gm_l5;
                            uint8_t t36 = ((t35 == ((uint8_t)34ULL)));
                            if (t36) {
                                uint64_t t37 = gm_l3;
                                uint64_t t38 = gm_l4;
                                gm_t2lqu8jsj511d t39;
                                GM_ST(uint64_t, ((uint8_t *)&t39 + 0), t37);
                                GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 0;
                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t39);
                                goto gm_fdone;
                            }
                            uint8_t t40 = gm_l5;
                            uint8_t t41 = (t40 < ((uint8_t)32ULL));
                            if (t41) {
                                uint64_t t42 = gm_l3;
                                gm_tekvgkj2hms37 t43;
                                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)5ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t43 + 8), t42);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t43);
                                goto gm_fdone;
                            }
                            uint8_t t44 = gm_l5;
                            uint8_t t45 = ((t44 == ((uint8_t)92ULL)));
                            if (t45) {
                                uint64_t t46 = gm_l3;
                                uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                                if (o48) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 488, 16);
                                uint64_t t49 = r47;
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint8_t t51 = (t49 >= t50);
                                if (t51) {
                                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                    gm_tekvgkj2hms37 t53;
                                    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
                                    GM_ST(uint64_t, ((uint8_t *)&t53 + 8), t52);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t53);
                                    goto gm_fdone;
                                }
                                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t56 = gm_l3;
                                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                                if (o58) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 489, 20);
                                uint64_t t59 = r57;
                                if ((uintptr_t)t59 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t59, t55, gm_srcge9itd5fie17, 489, 18);
                                uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t59 * 1));
                                gm_l6 = t60;
                                {
                                    {
                                        uint8_t t61 = gm_l6;
                                        uint8_t t62 = ((t61 == ((uint8_t)34ULL)));
                                        if (!t62) {
                                            goto gm_lb5;
                                        }
                                        gm_l7 = ((uint8_t)34ULL);
                                        goto gm_lb4;
                                    }
gm_lb5:;
                                    {
                                        uint8_t t63 = gm_l6;
                                        uint8_t t64 = ((t63 == ((uint8_t)92ULL)));
                                        if (!t64) {
                                            goto gm_lb6;
                                        }
                                        gm_l7 = ((uint8_t)92ULL);
                                        goto gm_lb4;
                                    }
gm_lb6:;
                                    {
                                        uint8_t t65 = gm_l6;
                                        uint8_t t66 = ((t65 == ((uint8_t)47ULL)));
                                        if (!t66) {
                                            goto gm_lb7;
                                        }
                                        gm_l7 = ((uint8_t)47ULL);
                                        goto gm_lb4;
                                    }
gm_lb7:;
                                    {
                                        uint8_t t67 = gm_l6;
                                        uint8_t t68 = ((t67 == ((uint8_t)98ULL)));
                                        if (!t68) {
                                            goto gm_lb8;
                                        }
                                        gm_l7 = ((uint8_t)8ULL);
                                        goto gm_lb4;
                                    }
gm_lb8:;
                                    {
                                        uint8_t t69 = gm_l6;
                                        uint8_t t70 = ((t69 == ((uint8_t)102ULL)));
                                        if (!t70) {
                                            goto gm_lb9;
                                        }
                                        gm_l7 = ((uint8_t)12ULL);
                                        goto gm_lb4;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t71 = gm_l6;
                                        uint8_t t72 = ((t71 == ((uint8_t)110ULL)));
                                        if (!t72) {
                                            goto gm_lb10;
                                        }
                                        gm_l7 = ((uint8_t)10ULL);
                                        goto gm_lb4;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t73 = gm_l6;
                                        uint8_t t74 = ((t73 == ((uint8_t)114ULL)));
                                        if (!t74) {
                                            goto gm_lb11;
                                        }
                                        gm_l7 = ((uint8_t)13ULL);
                                        goto gm_lb4;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t75 = gm_l6;
                                        uint8_t t76 = ((t75 == ((uint8_t)116ULL)));
                                        if (!t76) {
                                            goto gm_lb12;
                                        }
                                        gm_l7 = ((uint8_t)9ULL);
                                        goto gm_lb4;
                                    }
gm_lb12:;
                                    {
                                        gm_l7 = ((uint8_t)0ULL);
                                        goto gm_lb4;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 490, 26);
                                }
gm_lb4:;
                                uint8_t t77 = gm_l7;
                                uint8_t t78 = (!(t77 == ((uint8_t)0ULL)));
                                if (t78) {
                                    uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint8_t t80 = (t79 > ((uint64_t)0ULL));
                                    if (t80) {
                                        void * t81 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t83 = gm_l4;
                                        if ((uintptr_t)t83 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t83, t82, gm_srcge9itd5fie17, 502, 34);
                                        uint8_t t84 = gm_l7;
                                        GM_ST(uint8_t, (((uint8_t *)t81) + (uintptr_t)t83 * 1), t84);
                                    }
                                    uint64_t t85 = gm_l4;
                                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                                    if (o87) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 503, 17);
                                    uint64_t t88 = r86;
                                    gm_l4 = t88;
                                    uint64_t t89 = gm_l3;
                                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)2ULL), &r90);
                                    if (o91) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 504, 17);
                                    uint64_t t92 = r90;
                                    gm_l3 = t92;
                                } else {
                                    uint8_t t93 = gm_l6;
                                    uint8_t t94 = ((t93 == ((uint8_t)117ULL)));
                                    if (t94) {
                                        void * t95 = gm_l0;
                                        uint64_t t96 = gm_l3;
                                        uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)2ULL), &r97);
                                        if (o98) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 506, 35);
                                        uint64_t t99 = r97;
                                        gm_f_hex4_23k9kdb((void *)&gm_l12, t95, t99);
                                        uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                        if (t100) {
                                        } else {
                                            gm_tekvgkj2hms37 t101 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l12) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t101);
                                            goto gm_fdone;
                                        }
                                        uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
                                        gm_l8 = t102;
                                        gm_l9 = ((uint64_t)6ULL);
                                        {
                                            {
                                                uint32_t t103 = gm_l8;
                                                uint8_t t104 = (t103 >= ((uint32_t)55296ULL));
                                                if (!t104) {
                                                    goto gm_lb15;
                                                }
                                                uint32_t t105 = gm_l8;
                                                uint8_t t106 = (t105 <= ((uint32_t)56319ULL));
                                                if (!t106) {
                                                    goto gm_lb15;
                                                }
                                                uint64_t t107 = gm_l3;
                                                uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)12ULL), &r108);
                                                if (o109) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 24);
                                                uint64_t t110 = r108;
                                                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                uint8_t t112 = (t110 > t111);
                                                if (t112) {
                                                    gm_l14 = ((uint8_t)1);
                                                } else {
                                                    void * t113 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint64_t t115 = gm_l3;
                                                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)6ULL), &r116);
                                                    if (o117) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 44);
                                                    uint64_t t118 = r116;
                                                    if ((uintptr_t)t118 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t118, t114, gm_srcge9itd5fie17, 510, 42);
                                                    uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)t113) + (uintptr_t)t118 * 1));
                                                    uint8_t t120 = (!(t119 == ((uint8_t)92ULL)));
                                                    gm_l14 = t120;
                                                }
                                                uint8_t t121 = gm_l14;
                                                if (t121) {
                                                    gm_l13 = ((uint8_t)1);
                                                } else {
                                                    void * t122 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                    uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint64_t t124 = gm_l3;
                                                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)7ULL), &r125);
                                                    if (o126) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 68);
                                                    uint64_t t127 = r125;
                                                    if ((uintptr_t)t127 >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)t127, t123, gm_srcge9itd5fie17, 510, 66);
                                                    uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)t122) + (uintptr_t)t127 * 1));
                                                    uint8_t t129 = (!(t128 == ((uint8_t)117ULL)));
                                                    gm_l13 = t129;
                                                }
                                                uint8_t t130 = gm_l13;
                                                if (t130) {
                                                    uint64_t t131 = gm_l3;
                                                    gm_tekvgkj2hms37 t132;
                                                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)4ULL));
                                                    GM_ST(uint64_t, ((uint8_t *)&t132 + 8), t131);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t132);
                                                    goto gm_fdone;
                                                }
                                                void * t133 = gm_l0;
                                                uint64_t t134 = gm_l3;
                                                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)8ULL), &r135);
                                                if (o136) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 513, 39);
                                                uint64_t t137 = r135;
                                                gm_f_hex4_23k9kdb((void *)&gm_l15, t133, t137);
                                                uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                                if (t138) {
                                                } else {
                                                    gm_tekvgkj2hms37 t139 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&gm_l15) + 8));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t139);
                                                    goto gm_fdone;
                                                }
                                                uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 8));
                                                gm_l10 = t140;
                                                uint32_t t141 = gm_l10;
                                                uint8_t t142 = (t141 < ((uint32_t)56320ULL));
                                                if (t142) {
                                                    gm_l16 = ((uint8_t)1);
                                                } else {
                                                    uint32_t t143 = gm_l10;
                                                    uint8_t t144 = (t143 > ((uint32_t)57343ULL));
                                                    gm_l16 = t144;
                                                }
                                                uint8_t t145 = gm_l16;
                                                if (t145) {
                                                    uint64_t t146 = gm_l3;
                                                    gm_tekvgkj2hms37 t147;
                                                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)4ULL));
                                                    GM_ST(uint64_t, ((uint8_t *)&t147 + 8), t146);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t147);
                                                    goto gm_fdone;
                                                }
                                                uint32_t t148 = gm_l8;
                                                uint32_t r149; bool o150 = __builtin_sub_overflow(t148, ((uint32_t)55296ULL), &r149);
                                                if (o150) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 515, 38);
                                                uint32_t t151 = r149;
                                                unsigned __int128 n152 = (unsigned __int128)((uint64_t)10ULL);
                                                if (n152 >= 32) gm_trap_shift((uint64_t)n152, "u32", gm_srcge9itd5fie17, 515, 37);
                                                uint32_t r153 = (uint32_t)((uint32_t)t151 << (int)n152);
                                                bool o154 = (uint32_t)(r153 >> (int)n152) != t151;
                                                if (o154) gm_trap("integer overflow in `<<`", gm_srcge9itd5fie17, 515, 37);
                                                uint32_t t155 = r153;
                                                uint32_t r156; bool o157 = __builtin_add_overflow(((uint32_t)65536ULL), t155, &r156);
                                                if (o157) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 515, 26);
                                                uint32_t t158 = r156;
                                                uint32_t t159 = gm_l10;
                                                uint32_t r160; bool o161 = __builtin_sub_overflow(t159, ((uint32_t)56320ULL), &r160);
                                                if (o161) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 515, 61);
                                                uint32_t t162 = r160;
                                                uint32_t r163; bool o164 = __builtin_add_overflow(t158, t162, &r163);
                                                if (o164) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 515, 26);
                                                uint32_t t165 = r163;
                                                gm_l8 = t165;
                                                gm_l9 = ((uint64_t)12ULL);
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            {
                                                {
                                                    uint32_t t166 = gm_l8;
                                                    uint8_t t167 = (t166 >= ((uint32_t)56320ULL));
                                                    if (!t167) {
                                                        goto gm_lb17;
                                                    }
                                                    uint32_t t168 = gm_l8;
                                                    uint8_t t169 = (t168 <= ((uint32_t)57343ULL));
                                                    if (!t169) {
                                                        goto gm_lb17;
                                                    }
                                                    uint64_t t170 = gm_l3;
                                                    gm_tekvgkj2hms37 t171;
                                                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)4ULL));
                                                    GM_ST(uint64_t, ((uint8_t *)&t171 + 8), t170);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t171);
                                                    goto gm_fdone;
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                        }
gm_lb14:;
                                        uint64_t t172 = gm_l4;
                                        uint32_t t173 = gm_l8;
                                        gm_b16_8 t174 = gm_l1;
                                        uint64_t t175 = gm_l4;
                                        uint64_t t176 = gm_f_encode_utf8_s8ajso1(t173, t174, t175);
                                        uint64_t r177; bool o178 = __builtin_add_overflow(t172, t176, &r177);
                                        if (o178) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 520, 17);
                                        uint64_t t179 = r177;
                                        gm_l4 = t179;
                                        uint64_t t180 = gm_l3;
                                        uint64_t t181 = gm_l9;
                                        uint64_t r182; bool o183 = __builtin_add_overflow(t180, t181, &r182);
                                        if (o183) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 521, 17);
                                        uint64_t t184 = r182;
                                        gm_l3 = t184;
                                    } else {
                                        uint64_t t185 = gm_l3;
                                        uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                                        if (o187) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 523, 40);
                                        uint64_t t188 = r186;
                                        gm_tekvgkj2hms37 t189;
                                        GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)3ULL));
                                        GM_ST(uint64_t, ((uint8_t *)&t189 + 8), t188);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t189);
                                        goto gm_fdone;
                                    }
                                }
                            } else {
                                uint8_t t190 = gm_l5;
                                uint8_t t191 = (t190 < ((uint8_t)128ULL));
                                if (t191) {
                                    uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint8_t t193 = (t192 > ((uint64_t)0ULL));
                                    if (t193) {
                                        void * t194 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t196 = gm_l4;
                                        if ((uintptr_t)t196 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t196, t195, gm_srcge9itd5fie17, 526, 30);
                                        uint8_t t197 = gm_l5;
                                        GM_ST(uint8_t, (((uint8_t *)t194) + (uintptr_t)t196 * 1), t197);
                                    }
                                    uint64_t t198 = gm_l4;
                                    uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                                    if (o200) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 527, 13);
                                    uint64_t t201 = r199;
                                    gm_l4 = t201;
                                    uint64_t t202 = gm_l3;
                                    uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                                    if (o204) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 528, 13);
                                    uint64_t t205 = r203;
                                    gm_l3 = t205;
                                } else {
                                    gm_b16_8 t206 = gm_l2;
                                    uint64_t t207 = gm_l3;
                                    uint64_t t208 = gm_f_utf8_len_u8bogbn(t206, t207);
                                    gm_l11 = t208;
                                    uint64_t t209 = gm_l11;
                                    uint8_t t210 = ((t209 == ((uint64_t)0ULL)));
                                    if (t210) {
                                        uint64_t t211 = gm_l3;
                                        gm_tekvgkj2hms37 t212;
                                        GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)6ULL));
                                        GM_ST(uint64_t, ((uint8_t *)&t212 + 8), t211);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                        GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8), t212);
                                        goto gm_fdone;
                                    }
                                    uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint8_t t214 = (t213 > ((uint64_t)0ULL));
                                    if (t214) {
                                        void * t215 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t217 = gm_l4;
                                        uint64_t t218 = gm_l4;
                                        uint64_t t219 = gm_l11;
                                        uint64_t r220; bool o221 = __builtin_add_overflow(t218, t219, &r220);
                                        if (o221) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 532, 37);
                                        uint64_t t222 = r220;
                                        uint64_t t223 = (uintptr_t)t217;
                                        uint64_t t224 = (uintptr_t)t222;
                                        if (t223 > t224 || t224 > (uintptr_t)t216) gm_trap_slice(t223, t224, t216, gm_srcge9itd5fie17, 532, 30);
                                        uint64_t t225 = ((uintptr_t)t222) - (uintptr_t)t217;
                                        gm_b16_8 t226;
                                        GM_ST(void *, (uint8_t *)&t226, (void *)(((uint8_t *)t215 + (uintptr_t)t217 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(t225));
                                        void * t227 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                        uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                        uint64_t t229 = gm_l3;
                                        uint64_t t230 = gm_l3;
                                        uint64_t t231 = gm_l11;
                                        uint64_t r232; bool o233 = __builtin_add_overflow(t230, t231, &r232);
                                        if (o233) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 532, 59);
                                        uint64_t t234 = r232;
                                        uint64_t t235 = (uintptr_t)t229;
                                        uint64_t t236 = (uintptr_t)t234;
                                        if (t235 > t236 || t236 > (uintptr_t)t228) gm_trap_slice(t235, t236, t228, gm_srcge9itd5fie17, 532, 54);
                                        uint64_t t237 = ((uintptr_t)t234) - (uintptr_t)t229;
                                        gm_b16_8 t238;
                                        GM_ST(void *, (uint8_t *)&t238, (void *)(((uint8_t *)t227 + (uintptr_t)t229 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(t237));
                                        gm_f_copy_from_f7j6p05(t226, t238);
                                    }
                                    uint64_t t239 = gm_l4;
                                    uint64_t t240 = gm_l11;
                                    uint64_t r241; bool o242 = __builtin_add_overflow(t239, t240, &r241);
                                    if (o242) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 533, 13);
                                    uint64_t t243 = r241;
                                    gm_l4 = t243;
                                    uint64_t t244 = gm_l3;
                                    uint64_t t245 = gm_l11;
                                    uint64_t r246; bool o247 = __builtin_add_overflow(t244, t245, &r246);
                                    if (o247) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 534, 13);
                                    uint64_t t248 = r246;
                                    gm_l3 = t248;
                                }
                            }
                        }
                    }
gm_lb3:;
                }
            }
        }
gm_lb1:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1dp1jo1gr2pm gm_f_read_string_b2p27kc_c(void * a0, gm_b16_8 a1) { gm_t1dp1jo1gr2pm gm_r; gm_f_read_string_b2p27kc(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_skip_ws_g70e352(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_ws_g70e352 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
            uint8_t t9 = (t6 < t8);
            if (!t9) {
                goto gm_lb1;
            }
            {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, ((uint8_t *)t10));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t10) + 8));
                void * t13 = gm_l0;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
                if ((uintptr_t)t14 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t14, t12, gm_srcge9itd5fie17, 312, 14);
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t14 * 1));
                gm_l1 = t15;
                uint8_t t16 = gm_l1;
                uint8_t t17 = ((t16 == ((uint8_t)32ULL)));
                if (t17) {
                    gm_l4 = ((uint8_t)1);
                } else {
                    uint8_t t18 = gm_l1;
                    uint8_t t19 = ((t18 == ((uint8_t)9ULL)));
                    gm_l4 = t19;
                }
                uint8_t t20 = gm_l4;
                if (t20) {
                    gm_l3 = ((uint8_t)1);
                } else {
                    uint8_t t21 = gm_l1;
                    uint8_t t22 = ((t21 == ((uint8_t)10ULL)));
                    gm_l3 = t22;
                }
                uint8_t t23 = gm_l3;
                if (t23) {
                    gm_l2 = ((uint8_t)1);
                } else {
                    uint8_t t24 = gm_l1;
                    uint8_t t25 = ((t24 == ((uint8_t)13ULL)));
                    gm_l2 = t25;
                }
                uint8_t t26 = gm_l2;
                if (t26) {
                    void * t27 = gm_l0;
                    void * t28 = gm_l0;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 313, 62);
                    uint64_t t32 = r30;
                    GM_ST(uint64_t, (((uint8_t *)t27) + 16), t32);
                } else {
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
            }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_split_25tpoqf(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_25tpoqf F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* neg */
    uint64_t gm_l3; /* int_start */
    uint64_t gm_l4; /* int_end */
    uint64_t gm_l5; /* frac_start */
    uint64_t gm_l6; /* frac_end */
    int64_t gm_l7; /* exp */
    uint8_t gm_l8; /* eneg */
    GM_STACK_CHECK_SHALLOW(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((uint64_t)0ULL);
    gm_l2 = ((uint8_t)0);
    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t14, gm_srcge9itd5fie17, 175, 8);
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t16 = ((t15 == ((uint8_t)45ULL)));
    if (t16) {
        gm_l2 = ((uint8_t)1);
        gm_l1 = ((uint64_t)1ULL);
    }
    uint64_t t17 = gm_l1;
    gm_l3 = t17;
    {
        for (;;) {
            uint64_t t18 = gm_l1;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t23 = gm_l1;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
            uint8_t t25 = (t24 >= ((uint8_t)48ULL));
            if (!t25) {
                goto gm_lb1;
            }
            void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t28 = gm_l1;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
            uint8_t t30 = (t29 <= ((uint8_t)57ULL));
            if (!t30) {
                goto gm_lb1;
            }
            {
                uint64_t t31 = gm_l1;
                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 180, 55);
                uint64_t t34 = r32;
                gm_l1 = t34;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t35 = gm_l1;
    gm_l4 = t35;
    uint64_t t36 = gm_l1;
    gm_l5 = t36;
    uint64_t t37 = gm_l1;
    gm_l6 = t37;
    {
        {
            uint64_t t38 = gm_l1;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t40 = (t38 < t39);
            if (!t40) {
                goto gm_lb5;
            }
            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t43 = gm_l1;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t41) + (uintptr_t)t43 * 1));
            uint8_t t45 = ((t44 == ((uint8_t)46ULL)));
            if (!t45) {
                goto gm_lb5;
            }
            uint64_t t46 = gm_l1;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 185, 9);
            uint64_t t49 = r47;
            gm_l1 = t49;
            uint64_t t50 = gm_l1;
            gm_l5 = t50;
            {
                for (;;) {
                    uint64_t t51 = gm_l1;
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint8_t t53 = (t51 < t52);
                    if (!t53) {
                        goto gm_lb6;
                    }
                    void * t54 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t56 = gm_l1;
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t56 * 1));
                    uint8_t t58 = (t57 >= ((uint8_t)48ULL));
                    if (!t58) {
                        goto gm_lb6;
                    }
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t61 = gm_l1;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t59) + (uintptr_t)t61 * 1));
                    uint8_t t63 = (t62 <= ((uint8_t)57ULL));
                    if (!t63) {
                        goto gm_lb6;
                    }
                    {
                        uint64_t t64 = gm_l1;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        if (o66) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 187, 59);
                        uint64_t t67 = r65;
                        gm_l1 = t67;
                    }
gm_lb8:;
                }
            }
gm_lb6:;
            uint64_t t68 = gm_l1;
            gm_l6 = t68;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_l7 = ((int64_t)0LL);
    uint64_t t69 = gm_l1;
    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t71 = (t69 < t70);
    if (t71) {
        uint64_t t72 = gm_l1;
        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
        if (o74) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 193, 9);
        uint64_t t75 = r73;
        gm_l1 = t75;
        gm_l8 = ((uint8_t)0);
        void * t76 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t78 = gm_l1;
        if ((uintptr_t)t78 >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)t78, t77, gm_srcge9itd5fie17, 195, 12);
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)t76) + (uintptr_t)t78 * 1));
        uint8_t t80 = ((t79 == ((uint8_t)45ULL)));
        if (t80) {
            gm_l8 = ((uint8_t)1);
            uint64_t t81 = gm_l1;
            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
            if (o83) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 197, 13);
            uint64_t t84 = r82;
            gm_l1 = t84;
        } else {
            void * t85 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t87 = gm_l1;
            if ((uintptr_t)t87 >= (uintptr_t)(t86)) gm_trap_index((uintptr_t)t87, t86, gm_srcge9itd5fie17, 198, 19);
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)t85) + (uintptr_t)t87 * 1));
            uint8_t t89 = ((t88 == ((uint8_t)43ULL)));
            if (t89) {
                uint64_t t90 = gm_l1;
                uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                if (o92) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 199, 13);
                uint64_t t93 = r91;
                gm_l1 = t93;
            }
        }
        {
            for (;;) {
                uint64_t t94 = gm_l1;
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint8_t t96 = (t94 < t95);
                if (!t96) {
                    goto gm_lb9;
                }
                {
                    int64_t t97 = gm_l7;
                    uint8_t t98 = (t97 < ((int64_t)1000000000LL));
                    if (t98) {
                        int64_t t99 = gm_l7;
                        int64_t r100; bool o101 = __builtin_mul_overflow(t99, ((int64_t)10LL), &r100);
                        if (o101) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 202, 44);
                        int64_t t102 = r100;
                        void * t103 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t105 = gm_l1;
                        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)t103) + (uintptr_t)t105 * 1));
                        uint8_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint8_t)48ULL), &r107);
                        if (o108) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 202, 56);
                        uint8_t t109 = r107;
                        int64_t t110 = (int64_t)t109;
                        int64_t r111; bool o112 = __builtin_add_overflow(t102, t110, &r111);
                        if (o112) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 202, 44);
                        int64_t t113 = r111;
                        gm_l7 = t113;
                    }
                    uint64_t t114 = gm_l1;
                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                    if (o116) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 203, 13);
                    uint64_t t117 = r115;
                    gm_l1 = t117;
                }
gm_lb11:;
            }
        }
gm_lb9:;
        uint8_t t118 = gm_l8;
        if (t118) {
            int64_t t119 = gm_l7;
            if (t119 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 205, 25);
            int64_t t120 = (int64_t)((uint64_t)0 - (uint64_t)t119);
            gm_l7 = t120;
        }
    }
    uint8_t t121 = gm_l2;
    uint64_t t122 = gm_l3;
    uint64_t t123 = gm_l4;
    uint64_t t124 = gm_l5;
    uint64_t t125 = gm_l6;
    int64_t t126 = gm_l7;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 0), t121);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 8), t122);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 16), t123);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 24), t124);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 32), t125);
    GM_ST(int64_t, ((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 40), t126);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csnt0cc832rgst gm_f_split_25tpoqf_c(gm_b16_8 a0) { gm_t3072hbjtr5t6 gm_r; gm_f_split_25tpoqf(&gm_r, a0); return GM_CONV(gm_csnt0cc832rgst, gm_r); }
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_d1vui09(gm_tnpifv20irnph a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_i64_d1vui09 F;
    gm_tnpifv20irnph gm_l0; /* n */
    gm_tcs9tli4q9d6j gm_l2; /* nm */
    int64_t gm_l3; /* frac_len */
    int64_t gm_l4; /* int_len */
    int64_t gm_l5; /* total */
    int64_t gm_l6; /* e */
    unsigned __int128 gm_l7; /* v */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 953u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l13;
    uint8_t gm_l14;
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
        gm_f_split_25tpoqf((void *)&F.l1, t12);
    }
    {
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
        gm_t3072hbjtr5t6 t14 = GM_LD(gm_t3072hbjtr5t6, ((uint8_t *)&F.l1));
        gm_f_normalize_pcair89((void *)&gm_l2, t13, &t14);
    }
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
    if (t16) {
        gm_t913bb735pvt5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t17), ((int64_t)0LL));
        ((uint8_t *)&t17)[8] = 1;
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 32));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 24));
    uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 246, 18);
    uint64_t t22 = r20;
    int64_t t23 = (int64_t)t22;
    gm_l3 = t23;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 16));
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
    uint64_t r26; bool o27 = __builtin_sub_overflow(t24, t25, &r26);
    if (o27) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 247, 17);
    uint64_t t28 = r26;
    int64_t t29 = (int64_t)t28;
    gm_l4 = t29;
    int64_t t30 = gm_l4;
    int64_t t31 = gm_l3;
    int64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
    if (o33) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 248, 14);
    int64_t t34 = r32;
    gm_l5 = t34;
    int64_t t35 = GM_LD(int64_t, (((uint8_t *)&F.l1) + 40));
    int64_t t36 = gm_l3;
    int64_t r37; bool o38 = __builtin_sub_overflow(t35, t36, &r37);
    if (o38) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 249, 10);
    int64_t t39 = r37;
    int64_t t40 = gm_l5;
    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
    int64_t t42 = (int64_t)t41;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    int64_t t44 = (int64_t)t43;
    int64_t r45; bool o46 = __builtin_add_overflow(t42, t44, &r45);
    if (o46) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 249, 39);
    int64_t t47 = r45;
    int64_t r48; bool o49 = __builtin_sub_overflow(t40, t47, &r48);
    if (o49) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 249, 30);
    int64_t t50 = r48;
    int64_t r51; bool o52 = __builtin_add_overflow(t39, t50, &r51);
    if (o52) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 249, 10);
    int64_t t53 = r51;
    gm_l6 = t53;
    int64_t t54 = gm_l6;
    uint8_t t55 = (t54 < ((int64_t)0LL));
    if (t55) {
        gm_l14 = ((uint8_t)1);
    } else {
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t57 = (t56 > ((uint64_t)19ULL));
        gm_l14 = t57;
    }
    uint8_t t58 = gm_l14;
    if (t58) {
        gm_l13 = ((uint8_t)1);
    } else {
        int64_t t59 = gm_l6;
        uint8_t t60 = (t59 > ((int64_t)19LL));
        gm_l13 = t60;
    }
    uint8_t t61 = gm_l13;
    if (t61) {
        gm_t913bb735pvt5 t62;
        memset(&t62, 0, sizeof t62);
        F.ret = t62;
        goto gm_fret;
    }
    gm_l7 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l8 = t63;
    {
        for (;;) {
            uint64_t t64 = gm_l9;
            uint64_t t65 = gm_l8;
            uint8_t t66 = (t64 < t65);
            if (!t66) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t67 = gm_l9;
                        gm_l10 = t67;
                        unsigned __int128 t68 = gm_l7;
                        unsigned __int128 r69; bool o70 = __builtin_mul_overflow(t68, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)), &r69);
                        if (o70) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 252, 32);
                        unsigned __int128 t71 = r69;
                        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
                        uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                        uint64_t t74 = gm_l10;
                        uint64_t r75; bool o76 = __builtin_add_overflow(t73, t74, &r75);
                        if (o76) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 252, 62);
                        uint64_t t77 = r75;
                        uint8_t t78 = gm_f_digit_at_1oh5ota(t72, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)&F.l1)), t77);
                        uint8_t r79; bool o80 = __builtin_sub_overflow(t78, ((uint8_t)48ULL), &r79);
                        if (o80) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 252, 42);
                        uint8_t t81 = r79;
                        unsigned __int128 t82 = (unsigned __int128)t81;
                        unsigned __int128 r83; bool o84 = __builtin_add_overflow(t71, t82, &r83);
                        if (o84) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 252, 32);
                        unsigned __int128 t85 = r83;
                        gm_l7 = t85;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 252, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t86 = gm_l9;
            uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
            uint64_t t89 = r87;
            gm_l9 = t89;
        }
    }
gm_lb1:;
    gm_l12 = ((uint64_t)0ULL);
    int64_t t90 = gm_l6;
    uint64_t t91 = (uint64_t)t90;
    gm_l11 = t91;
    {
        for (;;) {
            uint64_t t92 = gm_l12;
            uint64_t t93 = gm_l11;
            uint8_t t94 = (t92 < t93);
            if (!t94) {
                goto gm_lb6;
            }
            {
                {
                    {
                        unsigned __int128 t95 = gm_l7;
                        unsigned __int128 r96; bool o97 = __builtin_mul_overflow(t95, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 10ULL)), &r96);
                        if (o97) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 254, 13);
                        unsigned __int128 t98 = r96;
                        gm_l7 = t98;
                        unsigned __int128 t99 = gm_l7;
                        uint8_t t100 = (t99 > ((unsigned __int128)(((unsigned __int128)1ULL << 64) | 0ULL)));
                        if (t100) {
                            gm_t913bb735pvt5 t101;
                            memset(&t101, 0, sizeof t101);
                            F.ret = t101;
                            goto gm_fret;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 253, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t102 = gm_l12;
            uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
            uint64_t t105 = r103;
            gm_l12 = t105;
        }
    }
gm_lb6:;
    uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
    if (t106) {
        unsigned __int128 t107 = gm_l7;
        uint8_t t108 = (t107 > ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 9223372036854775808ULL)));
        if (t108) {
            gm_t913bb735pvt5 t109;
            memset(&t109, 0, sizeof t109);
            F.ret = t109;
            goto gm_fret;
        }
        unsigned __int128 t110 = gm_l7;
        uint8_t t111 = ((t110 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 9223372036854775808ULL))));
        if (t111) {
            int64_t r112; bool o113 = __builtin_sub_overflow(((int64_t)-9223372036854775807LL), ((int64_t)1LL), &r112);
            if (o113) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 259, 46);
            int64_t t114 = r112;
            gm_t913bb735pvt5 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t115), t114);
            ((uint8_t *)&t115)[8] = 1;
            F.ret = t115;
            goto gm_fret;
        }
        unsigned __int128 t116 = gm_l7;
        int64_t t117 = (int64_t)t116;
        if (t117 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 260, 16);
        int64_t t118 = (int64_t)((uint64_t)0 - (uint64_t)t117);
        gm_t913bb735pvt5 t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t119), t118);
        ((uint8_t *)&t119)[8] = 1;
        F.ret = t119;
        goto gm_fret;
    }
    unsigned __int128 t120 = gm_l7;
    uint8_t t121 = (t120 > ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 9223372036854775807ULL)));
    if (t121) {
        gm_t913bb735pvt5 t122;
        memset(&t122, 0, sizeof t122);
        F.ret = t122;
        goto gm_fret;
    }
    unsigned __int128 t123 = gm_l7;
    int64_t t124 = (int64_t)t123;
    gm_t913bb735pvt5 t125;
    GM_ST(uint8_t, (uint8_t *)&t125 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t125 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t125 + 12, 0);
    GM_ST(int64_t, ((uint8_t *)&t125), t124);
    ((uint8_t *)&t125)[8] = 1;
    F.ret = t125;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_utf8_len_u8bogbn(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_utf8_len_u8bogbn F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* c */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* lo */
    uint8_t gm_l5; /* hi */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t15 = gm_l1;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcge9itd5fie17, 426, 10);
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
    gm_l2 = t16;
    uint8_t t17 = gm_l2;
    uint8_t t18 = (t17 < ((uint8_t)128ULL));
    if (t18) {
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
    }
    gm_l3 = ((uint64_t)0ULL);
    gm_l4 = ((uint8_t)128ULL);
    gm_l5 = ((uint8_t)191ULL);
    {
        {
            uint8_t t19 = gm_l2;
            uint8_t t20 = (t19 >= ((uint8_t)194ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint8_t t21 = gm_l2;
            uint8_t t22 = (t21 <= ((uint8_t)223ULL));
            if (!t22) {
                goto gm_lb2;
            }
            gm_l3 = ((uint64_t)2ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                uint8_t t23 = gm_l2;
                uint8_t t24 = (t23 >= ((uint8_t)224ULL));
                if (!t24) {
                    goto gm_lb4;
                }
                uint8_t t25 = gm_l2;
                uint8_t t26 = (t25 <= ((uint8_t)239ULL));
                if (!t26) {
                    goto gm_lb4;
                }
                gm_l3 = ((uint64_t)3ULL);
                uint8_t t27 = gm_l2;
                uint8_t t28 = ((t27 == ((uint8_t)224ULL)));
                if (t28) {
                    gm_l4 = ((uint8_t)160ULL);
                }
                uint8_t t29 = gm_l2;
                uint8_t t30 = ((t29 == ((uint8_t)237ULL)));
                if (t30) {
                    gm_l5 = ((uint8_t)159ULL);
                }
                goto gm_lb3;
            }
gm_lb4:;
            {
                {
                    uint8_t t31 = gm_l2;
                    uint8_t t32 = (t31 >= ((uint8_t)240ULL));
                    if (!t32) {
                        goto gm_lb6;
                    }
                    uint8_t t33 = gm_l2;
                    uint8_t t34 = (t33 <= ((uint8_t)244ULL));
                    if (!t34) {
                        goto gm_lb6;
                    }
                    gm_l3 = ((uint64_t)4ULL);
                    uint8_t t35 = gm_l2;
                    uint8_t t36 = ((t35 == ((uint8_t)240ULL)));
                    if (t36) {
                        gm_l4 = ((uint8_t)144ULL);
                    }
                    uint8_t t37 = gm_l2;
                    uint8_t t38 = ((t37 == ((uint8_t)244ULL)));
                    if (t38) {
                        gm_l5 = ((uint8_t)143ULL);
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                F.ret = ((uint64_t)0ULL);
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb3:;
    }
gm_lb1:;
    uint64_t t39 = gm_l1;
    uint64_t t40 = gm_l3;
    uint64_t r41; bool o42 = __builtin_add_overflow(t39, t40, &r41);
    if (o42) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 444, 8);
    uint64_t t43 = r41;
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t45 = (t43 > t44);
    if (t45) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t48 = gm_l1;
    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
    if (o50) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 445, 10);
    uint64_t t51 = r49;
    if ((uintptr_t)t51 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t51, t47, gm_srcge9itd5fie17, 445, 8);
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)t51 * 1));
    uint8_t t53 = gm_l4;
    uint8_t t54 = (t52 < t53);
    if (t54) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t55 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t57 = gm_l1;
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 445, 27);
        uint64_t t60 = r58;
        if ((uintptr_t)t60 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t60, t56, gm_srcge9itd5fie17, 445, 25);
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t60 * 1));
        uint8_t t62 = gm_l5;
        uint8_t t63 = (t61 > t62);
        gm_l9 = t63;
    }
    uint8_t t64 = gm_l9;
    if (t64) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    gm_l7 = ((uint64_t)2ULL);
    uint64_t t65 = gm_l3;
    gm_l6 = t65;
    {
        for (;;) {
            uint64_t t66 = gm_l7;
            uint64_t t67 = gm_l6;
            uint8_t t68 = (t66 < t67);
            if (!t68) {
                goto gm_lb7;
            }
            {
                {
                    {
                        uint64_t t69 = gm_l7;
                        gm_l8 = t69;
                        void * t70 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t72 = gm_l1;
                        uint64_t t73 = gm_l8;
                        uint64_t r74; bool o75 = __builtin_add_overflow(t72, t73, &r74);
                        if (o75) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 447, 14);
                        uint64_t t76 = r74;
                        if ((uintptr_t)t76 >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)t76, t71, gm_srcge9itd5fie17, 447, 12);
                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t70) + (uintptr_t)t76 * 1));
                        uint8_t t78 = (t77 < ((uint8_t)128ULL));
                        if (t78) {
                            gm_l10 = ((uint8_t)1);
                        } else {
                            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t81 = gm_l1;
                            uint64_t t82 = gm_l8;
                            uint64_t r83; bool o84 = __builtin_add_overflow(t81, t82, &r83);
                            if (o84) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 447, 33);
                            uint64_t t85 = r83;
                            if ((uintptr_t)t85 >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)t85, t80, gm_srcge9itd5fie17, 447, 31);
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)t85 * 1));
                            uint8_t t87 = (t86 > ((uint8_t)191ULL));
                            gm_l10 = t87;
                        }
                        uint8_t t88 = gm_l10;
                        if (t88) {
                            F.ret = ((uint64_t)0ULL);
                            goto gm_fret;
                        }
                        goto gm_lb10;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 446, 5);
                }
gm_lb10:;
            }
gm_lb9:;
            uint64_t t89 = gm_l7;
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
            uint64_t t92 = r90;
            gm_l7 = t92;
        }
    }
gm_lb7:;
    uint64_t t93 = gm_l3;
    F.ret = t93;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

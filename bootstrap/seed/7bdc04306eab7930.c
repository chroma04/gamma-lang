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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_t1d8endhr9k72;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; } gm_t1h4kdi7gs4v5;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; gm_b16_8 f4; uint64_t f5; uint64_t f6; } gm_t6s59i0k0msk8;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tr4ummvbupakb;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t1d8endhr9k72) == 40 && _Alignof(gm_t1d8endhr9k72) == 8 && offsetof(gm_t1d8endhr9k72, f0) == 0 && offsetof(gm_t1d8endhr9k72, f1) == 8 && offsetof(gm_t1d8endhr9k72, f2) == 24 && offsetof(gm_t1d8endhr9k72, f3) == 32, "layout of `(u64, string, u64, u64)`");
_Static_assert(sizeof(gm_t1h4kdi7gs4v5) == 64 && _Alignof(gm_t1h4kdi7gs4v5) == 8 && offsetof(gm_t1h4kdi7gs4v5, f0) == 0 && offsetof(gm_t1h4kdi7gs4v5, f1) == 16 && offsetof(gm_t1h4kdi7gs4v5, f2) == 32 && offsetof(gm_t1h4kdi7gs4v5, f3) == 40 && offsetof(gm_t1h4kdi7gs4v5, f4) == 56, "layout of `(string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s59i0k0msk8) == 72 && _Alignof(gm_t6s59i0k0msk8) == 8 && offsetof(gm_t6s59i0k0msk8, f0) == 0 && offsetof(gm_t6s59i0k0msk8, f1) == 8 && offsetof(gm_t6s59i0k0msk8, f2) == 24 && offsetof(gm_t6s59i0k0msk8, f3) == 32 && offsetof(gm_t6s59i0k0msk8, f4) == 40 && offsetof(gm_t6s59i0k0msk8, f5) == 56 && offsetof(gm_t6s59i0k0msk8, f6) == 64, "layout of `(u64, string, u64, u64, string, u64, u64)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tr4ummvbupakb) == 64 && _Alignof(gm_tr4ummvbupakb) == 8 && offsetof(gm_tr4ummvbupakb, f0) == 0 && offsetof(gm_tr4ummvbupakb, f1) == 16 && offsetof(gm_tr4ummvbupakb, f2) == 24 && offsetof(gm_tr4ummvbupakb, f3) == 32 && offsetof(gm_tr4ummvbupakb, f4) == 48 && offsetof(gm_tr4ummvbupakb, f5) == 56, "layout of `(string, u64, u64, string, u64, u64)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; } gm_te68172gglous;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_te68172gglous) == 304 && _Alignof(gm_te68172gglous) == 8 && offsetof(gm_te68172gglous, f0) == 0 && offsetof(gm_te68172gglous, f1) == 40 && offsetof(gm_te68172gglous, f2) == 80 && offsetof(gm_te68172gglous, f3) == 136 && offsetof(gm_te68172gglous, f4) == 192 && offsetof(gm_te68172gglous, f5) == 248, "layout of `Helpers`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_bytewise_eq_e8jgutj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_helper_k8mi0a4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_define_helper_c1utd1n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_fn_5udvcnu {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l32; /* sd */
    gm_t0mglksrkfa5e l39; /* fd */
    gm_tckdobunaulee l47; /* v */
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_1amnh72 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_39snre1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hhihlj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5f2vpdc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8nj2557 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8to6k7q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_edav5jt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eqjc9ro {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g3tqbla {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l1ii34d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lovbvbi {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_me0dr2l {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vd1blml {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_hash_elems_7gnr39c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_hash_fn_nlk82uj {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l23; /* sd */
    gm_t0mglksrkfa5e l29; /* fd */
    gm_tckdobunaulee l37; /* var */
    gm_b16_8 ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_scalar_pt0pjre {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_hash_step_ari7fns {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_helper_name_fob48de {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
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
struct gm_fr_less_fn_64um0ab {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l29; /* sd */
    gm_b16_8 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_helpers_4q783cn {
    gm_frame h;
    gm_te68172gglous ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_0kurvf5 {
    gm_frame h;
    gm_t1h4kdi7gs4v5 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2tci68e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_49bd3in {
    gm_frame h;
    gm_t1d8endhr9k72 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_4unpkmv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_551e4in {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_5ahb23t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_6nhr3v9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7k4ript {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7sl1fre {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8ijr07s {
    gm_frame h;
    gm_tg23pq9a29gh1 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8tfqc0p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_91uvm3b {
    gm_frame h;
    gm_thddbqb6snabm l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_96p75mj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_aakaioo {
    gm_frame h;
    gm_tr4ummvbupakb l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_aaqg4k2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_avsehn1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_baasn56 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_bjaiklv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_bq1tl8a {
    gm_frame h;
    gm_t6s59i0k0msk8 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_e15chue {
    gm_frame h;
    gm_tr4ummvbupakb l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ed06dp1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ho6u1fd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_idp5df3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ikd2l7g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jfl63ic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jgspi6j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jrf40pg {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_k88klp2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_k9lbjqb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ltl9pvt {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_o50khi1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_q1ni37m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_qvj9qj9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_sbsltkj {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4hhihlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8nj2557(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8to6k7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_edav5jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eqjc9ro(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g3tqbla(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_me0dr2l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_vd1blml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_elems_7gnr39c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_scalar_pt0pjre(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_step_ari7fns(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0kurvf5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1bkdsbn(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2tci68e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_49bd3in(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4unpkmv(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_52n62mc(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_551e4in(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ahb23t(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5qef5td(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6nhr3v9(void * a0, gm_t2lqu8jsj511d a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7k4ript(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7sl1fre(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_91uvm3b(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_96p75mj(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9bp3v08(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aakaioo(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aaqg4k2(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_au8sbv8(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_avsehn1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bjaiklv(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f98j2rj(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fileu9p(void * a0, gm_t2lqu8jsj511d a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fp54odu(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hndtg2g(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ho6u1fd(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_idp5df3(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ikd2l7g(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jfl63ic(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jgspi6j(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jrf40pg(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k88klp2(void * a0, gm_t2lqu8jsj511d a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k9lbjqb(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lhs6si1(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ltl9pvt(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_q1ni37m(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qvj9qj9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sbsltkj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uchttik(void * a0, const void * a1, void * a2, gm_b16_8 a3);
extern const uint8_t gm_s0si75ormd2gn[15];
extern const uint8_t gm_s1g2rkhcikvcj[26];
extern const uint8_t gm_s1jaan454qth6[19];
extern const uint8_t gm_s1n6uk6814mmb[53];
extern const uint8_t gm_s27f321qjn1nc[86];
extern const uint8_t gm_s3o95oair6jns[57];
extern const uint8_t gm_s4231ufcv20f0[12];
extern const uint8_t gm_s4fips0vo4jt3[8];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s6ug9r8dlfd2a[44];
extern const uint8_t gm_s81r46mpa8ra9[29];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9iu5gsqog7uc[5];
extern const uint8_t gm_s9muc7eh7ou36[42];
extern const uint8_t gm_s9qv939r7cv21[88];
extern const uint8_t gm_sa6br7uea4stj[69];
extern const uint8_t gm_sa8vf0vlkso00[30];
extern const uint8_t gm_sabd09jov3efr[32];
extern const uint8_t gm_sai6qda00m87d[49];
extern const uint8_t gm_sb1skofqksjcc[16];
extern const uint8_t gm_sb20a9eb6tt9l[50];
extern const uint8_t gm_sb5efqp7fp176[49];
extern const uint8_t gm_sc758i4r41ge5[128];
extern const uint8_t gm_sccpc5t5dl8ni[51];
extern const uint8_t gm_sciq0a78hirud[25];
extern const uint8_t gm_sd667aplubiqu[34];
extern const uint8_t gm_sdori1qhqiv1t[42];
extern const uint8_t gm_sdvlseva8qvl4[77];
extern const uint8_t gm_seb1p7qlbq9p0[39];
extern const uint8_t gm_sebe0816umkgp[54];
extern const uint8_t gm_sel1fj24qk32p[15];
extern const uint8_t gm_sf8mbf71lsfeh[35];
extern const uint8_t gm_sfftb05vrc1qn[81];
extern const uint8_t gm_sfm0hseeqersg[25];
extern const uint8_t gm_sfocmetde6c9f[26];
extern const uint8_t gm_sgdvb6gobbii2[8];
extern const uint8_t gm_sge6a56sqn6kj[21];
extern const uint8_t gm_sgn3gq6gfib7b[40];
extern const uint8_t gm_shm5oj4temhh6[33];
extern const uint8_t gm_shmmkvp83pr05[18];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_sihsae37m31pr[15];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjh4l64ncn038[52];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sl6pi8la5phb6[20];
extern const uint8_t gm_slq0l63qf1dvs[11];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_snggon8entbp9[32];
extern const uint8_t gm_snst06bp2i1vp[23];
extern const uint8_t gm_sog2vuuvk361m[22];
extern const uint8_t gm_sopln0aep28du[47];
extern const uint8_t gm_sor8flejv9lfq[67];
extern const uint8_t gm_sorc6ciip4ja7[51];
extern const uint8_t gm_sp2m434o2c1ha[3];
extern const uint8_t gm_sp68fbgfk0baj[39];
extern const uint8_t gm_sp8h6qkr4b6hg[12];
extern const uint8_t gm_spj5hvik290iq[41];
extern const uint8_t gm_sqg1k42secsds[29];
extern const uint8_t gm_sqg70d5uem21e[35];
extern const uint8_t gm_sqic2de8m2pet[61];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckg9scnip7e8g[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss6esonundp29[56];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_sud4o49sgtb17[28];
extern const uint8_t gm_sug64ltbhov0m[19];
extern const uint8_t gm_sup5ecv5jiqbn[119];
extern const uint8_t gm_suqqp23fdh2ff[6];
extern const uint8_t gm_surrjo1qo53mh[3];
extern const uint8_t gm_svqnr5kmirn0g[45];
extern const uint8_t gm_svr80l31985v4[7];
extern const gm_tmpl gm_tpl1r51q3hoe535;
extern const gm_tmpl gm_tpl2aofg9dsfuds;
extern const gm_tmpl gm_tpl2fh423sp4pd0;
extern const gm_tmpl gm_tpl34g57qfcenfh;
extern const gm_tmpl gm_tpl3kt8q9c3ih5v;
extern const gm_tmpl gm_tpl3ttr7njhqa54;
extern const gm_tmpl gm_tpl4dq73nmettt5;
extern const gm_tmpl gm_tpl5kl598jq5h12;
extern const gm_tmpl gm_tpl5svfna5ij5ui;
extern const gm_tmpl gm_tpl6muahgpe70pr;
extern const gm_tmpl gm_tpl6n3s5f3ittsi;
extern const gm_tmpl gm_tpl83pfuovi474u;
extern const gm_tmpl gm_tpl8s825rf2tq3f;
extern const gm_tmpl gm_tpl9s09hqjpb581;
extern const gm_tmpl gm_tplaotru3rka6ku;
extern const gm_tmpl gm_tplc5cre0uqposd;
extern const gm_tmpl gm_tplcqfaq9vjn7hh;
extern const gm_tmpl gm_tpldknbfev12jrb;
extern const gm_tmpl gm_tpleb7ph7fjnlbm;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplg21rd4as3ph6;
extern const gm_tmpl gm_tplh0kdp5708tgq;
extern const gm_tmpl gm_tplha5n8a69m774;
extern const gm_tmpl gm_tplhrko1gs9n6tk;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tpll060ppa56f8b;
extern const gm_tmpl gm_tplle7qvdq2jai6;
extern const gm_tmpl gm_tplm6qp3tlbq28j;
extern const gm_tmpl gm_tplmo458jpb0n1e;
extern const gm_tmpl gm_tplmpsv5qagthkk;
extern const gm_tmpl gm_tplmurfi7rvdamc;
extern const gm_tmpl gm_tplmv1ukn138isf;
extern const gm_tmpl gm_tpln8mihgcsij40;
extern const gm_tmpl gm_tplne0l0lciidvr;
extern const gm_tmpl gm_tplnud722tu4rrk;
extern const gm_tmpl gm_tplo8bto3ii8iq9;
extern const gm_tmpl gm_tplqd2g84l5ne39;
extern const gm_tmpl gm_tplr7vr7aubja28;
extern const gm_tmpl gm_tplr9mrmvfj4528;
extern const gm_tmpl gm_tplre7l7cu4fpbv;
extern const gm_tmpl gm_tplshk2l4vr6fa3;
extern const gm_tmpl gm_tplt3iub1goev8r;
extern const gm_tmpl gm_tpltj7umjd53udq;
extern const gm_tmpl gm_tplu0rdj3ds7u0l;
extern const gm_tmpl gm_tpluc8r0n3peqad;
extern const gm_tmpl gm_tpluf7u812t4p3d;
extern const gm_tmpl gm_tplvm3qupt9dved;
extern const gm_tmpl gm_tplvq03d5skib28;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eqb2nno7n67r(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_1amnh72 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_39snre1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suqqp23fdh2ff));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3ttr7njhqa54), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4hhihlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hhihlj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_seb1p7qlbq9p0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleb7ph7fjnlbm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5f2vpdc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sopln0aep28du));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluc8r0n3peqad), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8n626pi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8nj2557(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8nj2557 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4fips0vo4jt3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4dq73nmettt5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8to6k7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8to6k7q F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdori1qhqiv1t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu0rdj3ds7u0l), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_edav5jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_edav5jt F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sihsae37m31pr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqd2g84l5ne39), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eqjc9ro(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eqjc9ro F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4231ufcv20f0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplre7l7cu4fpbv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_g3tqbla(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_g3tqbla F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svr80l31985v4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl83pfuovi474u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l1ii34d F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd667aplubiqu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplc5cre0uqposd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lovbvbi F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqg1k42secsds));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg21rd4as3ph6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_me0dr2l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_me0dr2l F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfocmetde6c9f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr9mrmvfj4528), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vd1blml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vd1blml F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjh4l64ncn038));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnud722tu4rrk), t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_8n626pi((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcht195tfrfuqo, 82, 91);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b16_8 t10 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t2, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
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
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_m04clhi((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0kurvf5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_0kurvf5 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss6esonundp29));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_tftb1odc739t5 t4 = gm_f_write_1bkdsbn(t2, &(*(gm_t1h4kdi7gs4v5 *)((uint8_t *)a1)), ((void *)&gm_tplle7qvdq2jai6), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2tci68e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2tci68e F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1g2rkhcikvcj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl2aofg9dsfuds), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_49bd3in(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_49bd3in F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s27f321qjn1nc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(85));
    gm_tftb1odc739t5 t4 = gm_f_write_au8sbv8(t2, &(*(gm_t1d8endhr9k72 *)((uint8_t *)a1)), ((void *)&gm_tplha5n8a69m774), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4unpkmv(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_4unpkmv F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sai6qda00m87d));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(48));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpl34g57qfcenfh), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_551e4in(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_551e4in F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sabd09jov3efr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_tftb1odc739t5 t4 = gm_f_write_9bp3v08(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpl8s825rf2tq3f), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ahb23t(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5ahb23t F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sb1skofqksjcc));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(15));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplh0kdp5708tgq), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6nhr3v9(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_6nhr3v9 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s1n6uk6814mmb));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(52));
    gm_tftb1odc739t5 t5 = gm_f_write_fileu9p(t2, t3, ((void *)&gm_tpl6n3s5f3ittsi), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7k4ript(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7k4ript F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sb5efqp7fp176));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(48));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplr7vr7aubja28), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7sl1fre(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7sl1fre F;
    void * gm_l0; /* l */
    gm_tgbgti82mf879 gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6ug9r8dlfd2a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_tftb1odc739t5 t4 = gm_f_write_hndtg2g(t2, &(*(gm_tgbgti82mf879 *)((uint8_t *)a1)), ((void *)&gm_tpldknbfev12jrb), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8ijr07s F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb20a9eb6tt9l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_tftb1odc739t5 t4 = gm_f_write_52n62mc(t2, &(*(gm_tg23pq9a29gh1 *)((uint8_t *)a1)), ((void *)&gm_tpln8mihgcsij40), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8tfqc0p F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa6br7uea4stj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(68));
    gm_tftb1odc739t5 t5 = gm_f_write_fileu9p(t2, t3, ((void *)&gm_tplshk2l4vr6fa3), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_91uvm3b(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_91uvm3b F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sccpc5t5dl8ni));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_tftb1odc739t5 t4 = gm_f_write_f98j2rj(t2, &(*(gm_thddbqb6snabm *)((uint8_t *)a1)), ((void *)&gm_tplaotru3rka6ku), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_96p75mj(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_96p75mj F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sog2vuuvk361m));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(21));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl2fh423sp4pd0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aakaioo(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_aakaioo F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc758i4r41ge5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(127));
    gm_tftb1odc739t5 t4 = gm_f_write_fp54odu(t2, &(*(gm_tr4ummvbupakb *)((uint8_t *)a1)), ((void *)&gm_tpl9s09hqjpb581), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aaqg4k2(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_aaqg4k2 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sgdvb6gobbii2));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplmurfi7rvdamc), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_avsehn1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_avsehn1 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9muc7eh7ou36));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl1r51q3hoe535), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_baasn56 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_svqnr5kmirn0g));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(44));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpluf7u812t4p3d), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bjaiklv(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_bjaiklv F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa8vf0vlkso00));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(29));
    gm_tftb1odc739t5 t5 = gm_f_write_5qef5td(t2, t3, ((void *)&gm_tplhrko1gs9n6tk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_bq1tl8a F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqic2de8m2pet));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
    gm_tftb1odc739t5 t4 = gm_f_write_uchttik(t2, &(*(gm_t6s59i0k0msk8 *)((uint8_t *)a1)), ((void *)&gm_tpl5kl598jq5h12), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_e15chue F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3o95oair6jns));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_tftb1odc739t5 t4 = gm_f_write_fp54odu(t2, &(*(gm_tr4ummvbupakb *)((uint8_t *)a1)), ((void *)&gm_tplmv1ukn138isf), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ed06dp1 F;
    void * gm_l0; /* l */
    gm_tf1bjg81u0glv gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snggon8entbp9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_tftb1odc739t5 t4 = gm_f_write_lhs6si1(t2, &(*(gm_tf1bjg81u0glv *)((uint8_t *)a1)), ((void *)&gm_tplt3iub1goev8r), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ho6u1fd(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ho6u1fd F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sorc6ciip4ja7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(50));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpltj7umjd53udq), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_idp5df3(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_idp5df3 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sf8mbf71lsfeh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplmpsv5qagthkk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ikd2l7g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ikd2l7g F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spj5hvik290iq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl6muahgpe70pr), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jfl63ic(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jfl63ic F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0si75ormd2gn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(14));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvm3qupt9dved), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jgspi6j(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jgspi6j F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp68fbgfk0baj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(38));
    gm_tftb1odc739t5 t5 = gm_f_write_fileu9p(t2, t3, ((void *)&gm_tplo8bto3ii8iq9), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jrf40pg(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jrf40pg F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sud4o49sgtb17));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl5svfna5ij5ui), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k88klp2(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_k88klp2 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sdvlseva8qvl4));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(76));
    gm_tftb1odc739t5 t5 = gm_f_write_fileu9p(t2, t3, ((void *)&gm_tplcqfaq9vjn7hh), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k9lbjqb(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_k9lbjqb F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sfftb05vrc1qn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(80));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplvq03d5skib28), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ltl9pvt(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ltl9pvt F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snst06bp2i1vp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplne0l0lciidvr), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_o50khi1 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sebe0816umkgp));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(53));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpll060ppa56f8b), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_q1ni37m(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_q1ni37m F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp8h6qkr4b6hg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(11));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplm6qp3tlbq28j), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qvj9qj9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qvj9qj9 F;
    void * gm_l0; /* l */
    gm_tgbgti82mf879 gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9qv939r7cv21));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(87));
    gm_tftb1odc739t5 t4 = gm_f_write_hndtg2g(t2, &(*(gm_tgbgti82mf879 *)((uint8_t *)a1)), ((void *)&gm_tplmo458jpb0n1e), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sbsltkj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_sbsltkj F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1jaan454qth6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl3kt8q9c3ih5v), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bytewise_eq_e8jgutj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* bits */
    uint64_t gm_l3; /* storage */
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    gm_t5v8n6qrn4rh4 gm_l5;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l5, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l2 = t9;
            uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)&gm_l5) + 8) + 8));
            gm_l3 = t10;
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = gm_l3;
            uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)8ULL), &r14);
            if (o15) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 147, 44);
            uint64_t t16 = r14;
            uint8_t t17 = ((t12 == t16));
            gm_l4 = t17;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 146, 62);
    }
gm_lb1:;
    uint8_t t18 = gm_l4;
    F.ret = t18;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_declare_helper_k8mi0a4 F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    void * gm_l2; /* memo */
    gm_b16_8 gm_l3; /* kind */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* ret */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 342u);
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
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    gm_b16_8 t4 = gm_l3;
    gm_t88rcucg1b7uk t5 = gm_l4;
    gm_b16_8 t6 = gm_f_helper_name_fob48de((void *)NULL, cx, NULL, t2, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t6;
    void * t7 = gm_l2;
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
    gm_b16_8 t9 = gm_l7;
    gm_tftb1odc739t5 t10 = gm_f_put_po09vud(t7, t8, t9);
    gm_l8 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckg9scnip7e8g, 35, 5);
    void * t12 = gm_l0;
    void * t13 = (void *)((uint8_t *)t12);
    gm_b16_8 t14 = gm_l5;
    gm_b16_8 t15 = gm_l7;
    gm_b16_8 t16 = gm_l6;
    gm_tfeab279c16rr t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 32), t16);
    gm_tftb1odc739t5 t18 = gm_f_write_sbsltkj(t13, &t17);
    gm_l9 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srckg9scnip7e8g, 36, 5);
    gm_b16_8 t20 = gm_l7;
    F.ret = t20;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_define_helper_c1utd1n F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* ret */
    gm_b16_8 gm_l3; /* params */
    gm_b16_8 gm_l4; /* body */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_l1;
    gm_b16_8 t6 = gm_l3;
    gm_tfeab279c16rr t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 32), t6);
    gm_tftb1odc739t5 t8 = gm_f_write_jrf40pg(t3, &t7);
    gm_l5 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckg9scnip7e8g, 44, 5);
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 40);
    gm_b16_8 t12 = gm_l4;
    gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
    gm_l6 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srckg9scnip7e8g, 45, 5);
    void * t15 = gm_l0;
    void * t16 = (void *)(((uint8_t *)t15) + 40);
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sikpqtja8pqpv));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
    gm_l7 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srckg9scnip7e8g, 46, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_eq_fn_5udvcnu F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* f */
    uint64_t gm_l15; /* len */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    gm_b16_8 gm_l18; /* f */
    gm_t88rcucg1b7uk gm_l19; /* elem */
    uint64_t gm_l20; /* esz */
    gm_b16_8 gm_l21; /* f */
    gm_t88rcucg1b7uk gm_l22; /* inner */
    gm_b16_8 gm_l23; /* f */
    uint64_t gm_l24; /* flag */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* fe */
    gm_b16_8 gm_l28; /* fo */
    gm_b16_8 gm_l29; /* offs */
    uint64_t gm_l30; /* tg */
    uint64_t gm_l31; /* pl */
    gm_t88rcucg1b7uk gm_l33; /* u */
    gm_b16_8 gm_l34; /* f */
    gm_b16_8 gm_l35; /* offs */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* f */
    gm_t88rcucg1b7uk gm_l41; /* tag */
    gm_b16_8 gm_l42; /* variants */
    gm_b16_8 gm_l43; /* offs */
    gm_b16_8 gm_l44; /* tc */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_t88rcucg1b7uk gm_l48; /* pt */
    gm_b16_8 gm_l49; /* f */
    GM_STACK_CHECK(&F, 3800u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l50;
    gm_t5v8n6qrn4rh4 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_t53at80pfdus3 gm_l54;
    gm_tlcm4lvu3g1a8 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_b16_8 gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_t8ta482bl6be0 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tftb1odc739t5 gm_l81;
    {
        {
            {
                void * t43 = gm_l0;
                void * t44 = (void *)(((uint8_t *)t43) + 80);
                uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                gm_f_get_gnmase9((void *)&gm_l50, t44, t45);
                uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l50))[16] != 0);
                if (!t46) {
                    goto gm_lb2;
                }
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l50));
                gm_l3 = t47;
                gm_b16_8 t48 = gm_l3;
                F.ret = t48;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t49 = gm_l0;
        void * t50 = gm_l1;
        void * t51 = gm_l0;
        void * t52 = (void *)(((uint8_t *)t51) + 80);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sp2m434o2c1ha));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t54 = gm_l2;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(4));
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(34));
        gm_b16_8 t57 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t49, t50, t52, t53, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t57;
        gm_trqfvmjtt4sij t58;
        gm_f_list_uvfu1k1((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t58;
        void * t59 = gm_l1;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
        gm_l6 = t60;
        void * t61 = gm_l1;
        gm_t88rcucg1b7uk t62 = gm_l2;
        gm_f_repr_53quqki((void *)&gm_l51, (void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                {
                    {
                        {
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)0ULL));
                            if (!t64) {
                                goto gm_lb6;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)1ULL));
                            if (!t66) {
                                goto gm_lb7;
                            }
                            goto gm_lb5;
                        }
gm_lb7:;
                        {
                            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)3ULL));
                            if (!t68) {
                                goto gm_lb8;
                            }
                            goto gm_lb5;
                        }
gm_lb8:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    void * t69 = gm_l1;
                    gm_t88rcucg1b7uk t70 = gm_l2;
                    gm_b16_8 t71 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t69, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t71;
                    void * t72 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t73 = gm_l7;
                    gm_b16_8 t74 = gm_l7;
                    gm_t6j6smj7qoctu t75;
                    GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t73);
                    GM_ST(gm_b16_8, ((uint8_t *)&t75 + 16), t74);
                    gm_tftb1odc739t5 t76 = gm_f_write_avsehn1(t72, &t75);
                    gm_l52 = t76;
                    uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                    if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srckg9scnip7e8g, 58, 13);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)2ULL));
                    if (!t79) {
                        goto gm_lb9;
                    }
                    void * t80 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sgn3gq6gfib7b));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(39));
                    gm_tftb1odc739t5 t82 = gm_f_extend_0cs85gs(t80, t81);
                    gm_l53 = t82;
                    uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srckg9scnip7e8g, 60, 18);
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    {
                        void * t84 = gm_l1;
                        gm_t88rcucg1b7uk t85 = gm_l2;
                        gm_f_kind_ghvav22((void *)&gm_l54, t84, GM_CONV(gm_csguankko4lmhd, t85));
                        {
                            {
                                {
                                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)12ULL));
                                    if (!t87) {
                                        goto gm_lb12;
                                    }
                                    gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                                    gm_l8 = t88;
                                    void * t89 = gm_l1;
                                    gm_t88rcucg1b7uk t90 = gm_l2;
                                    gm_b16_8 t91 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t89, t90);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l9 = t91;
                                    gm_b16_8 t92 = gm_l8;
                                    gm_l10 = t92;
                                    gm_l11 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t93 = gm_l11;
                                            uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint8_t t95 = (t93 < t94);
                                            if (!t95) {
                                                goto gm_lb13;
                                            }
                                            {
                                                uint64_t t96 = gm_l11;
                                                void * t97 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t99 = gm_l11;
                                                gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + (uintptr_t)t99 * 4));
                                                gm_tlcm4lvu3g1a8 t101;
                                                GM_ST(uint32_t, (uint8_t *)&t101 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t101 + 0), t96);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t101 + 8), t100);
                                                gm_l55 = t101;
                                                {
                                                    {
                                                        uint64_t t102 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                                        gm_l12 = t102;
                                                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l55) + 8));
                                                        gm_l13 = t103;
                                                        void * t104 = gm_l0;
                                                        void * t105 = gm_l1;
                                                        gm_t88rcucg1b7uk t106 = gm_l13;
                                                        gm_b16_8 t107 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t104, t105, t106);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l14 = t107;
                                                        void * t108 = (void *)((uint8_t *)&F.l5);
                                                        gm_b16_8 t109 = gm_l14;
                                                        void * t110 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                        uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                        uint64_t t112 = gm_l12;
                                                        if ((uintptr_t)t112 >= (uintptr_t)(t111)) gm_trap_index((uintptr_t)t112, t111, gm_srckg9scnip7e8g, 66, 80);
                                                        uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t110) + (uintptr_t)t112 * 8));
                                                        void * t114 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                        uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                        uint64_t t116 = gm_l12;
                                                        if ((uintptr_t)t116 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t116, t115, gm_srckg9scnip7e8g, 66, 89);
                                                        uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)t114) + (uintptr_t)t116 * 8));
                                                        gm_tgbgti82mf879 t118;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t109);
                                                        GM_ST(uint64_t, ((uint8_t *)&t118 + 16), t113);
                                                        GM_ST(uint64_t, ((uint8_t *)&t118 + 24), t117);
                                                        gm_tftb1odc739t5 t119 = gm_f_write_7sl1fre(t108, &t118);
                                                        gm_l56 = t119;
                                                        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srckg9scnip7e8g, 66, 21);
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 64, 17);
                                                }
gm_lb16:;
                                            }
gm_lb15:;
                                            uint64_t t121 = gm_l11;
                                            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                            uint64_t t124 = r122;
                                            gm_l11 = t124;
                                        }
                                    }
gm_lb13:;
                                    void * t125 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t126;
                                    GM_ST(void *, (uint8_t *)&t126, (void *)(gm_shmmkvp83pr05));
                                    GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(17));
                                    gm_tftb1odc739t5 t127 = gm_f_extend_0cs85gs(t125, t126);
                                    gm_l57 = t127;
                                    uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                    if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srckg9scnip7e8g, 68, 17);
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
                            {
                                {
                                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)11ULL));
                                    if (!t130) {
                                        goto gm_lb18;
                                    }
                                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 8));
                                    gm_l15 = t131;
                                    gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l54) + 8) + 8));
                                    gm_l16 = t132;
                                    void * t133 = gm_l1;
                                    gm_t88rcucg1b7uk t134 = gm_l16;
                                    uint64_t t135 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t133, t134);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l17 = t135;
                                    void * t136 = gm_l1;
                                    gm_t88rcucg1b7uk t137 = gm_l16;
                                    uint8_t t138 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t136, t137);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    if (t138) {
                                        void * t139 = (void *)((uint8_t *)&F.l5);
                                        uint64_t t140 = gm_l15;
                                        uint64_t t141 = gm_l17;
                                        uint64_t r142; bool o143 = __builtin_mul_overflow(t140, t141, &r142);
                                        if (o143) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 73, 68);
                                        uint64_t t144 = r142;
                                        gm_t4ce0jd3tdro4 t145;
                                        GM_ST(uint64_t, ((uint8_t *)&t145 + 0), t144);
                                        gm_tftb1odc739t5 t146 = gm_f_write_idp5df3(t139, t145);
                                        gm_l58 = t146;
                                        uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srckg9scnip7e8g, 73, 21);
                                    } else {
                                        void * t148 = gm_l0;
                                        void * t149 = gm_l1;
                                        gm_t88rcucg1b7uk t150 = gm_l16;
                                        gm_b16_8 t151 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t148, t149, t150);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l18 = t151;
                                        void * t152 = (void *)((uint8_t *)&F.l5);
                                        uint64_t t153 = gm_l15;
                                        gm_b16_8 t154 = gm_l18;
                                        uint64_t t155 = gm_l17;
                                        uint64_t t156 = gm_l17;
                                        gm_t1d8endhr9k72 t157;
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 0), t153);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 8), t154);
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 24), t155);
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 32), t156);
                                        gm_tftb1odc739t5 t158 = gm_f_write_49bd3in(t152, &t157);
                                        gm_l59 = t158;
                                        uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                        if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srckg9scnip7e8g, 76, 21);
                                        void * t160 = (void *)((uint8_t *)&F.l5);
                                        gm_b16_8 t161;
                                        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_shmmkvp83pr05));
                                        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(17));
                                        gm_tftb1odc739t5 t162 = gm_f_extend_0cs85gs(t160, t161);
                                        gm_l60 = t162;
                                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                        if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srckg9scnip7e8g, 77, 21);
                                    }
                                    goto gm_lb11;
                                }
gm_lb18:;
                                {
                                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)10ULL));
                                    if (!t165) {
                                        goto gm_lb19;
                                    }
                                    gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l54) + 8) + 4));
                                    gm_l19 = t166;
                                    void * t167 = gm_l1;
                                    gm_t88rcucg1b7uk t168 = gm_l19;
                                    uint64_t t169 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t167, t168);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l20 = t169;
                                    void * t170 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t171 = gm_l6;
                                    gm_t4ce0jd3tdro4 t172;
                                    GM_ST(uint64_t, ((uint8_t *)&t172 + 0), t171);
                                    gm_tftb1odc739t5 t173 = gm_f_write_baasn56(t170, t172);
                                    gm_l61 = t173;
                                    uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                    if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srckg9scnip7e8g, 82, 17);
                                    void * t175 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t176 = gm_l6;
                                    gm_t4ce0jd3tdro4 t177;
                                    GM_ST(uint64_t, ((uint8_t *)&t177 + 0), t176);
                                    gm_tftb1odc739t5 t178 = gm_f_write_o50khi1(t175, t177);
                                    gm_l62 = t178;
                                    uint8_t t179 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                    if (!t179) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srckg9scnip7e8g, 83, 17);
                                    void * t180 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t181;
                                    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_sor8flejv9lfq));
                                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(66));
                                    gm_tftb1odc739t5 t182 = gm_f_extend_0cs85gs(t180, t181);
                                    gm_l63 = t182;
                                    uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                    if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srckg9scnip7e8g, 84, 17);
                                    void * t184 = gm_l1;
                                    gm_t88rcucg1b7uk t185 = gm_l19;
                                    uint8_t t186 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t184, t185);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    if (t186) {
                                        void * t187 = (void *)((uint8_t *)&F.l5);
                                        uint64_t t188 = gm_l20;
                                        gm_t4ce0jd3tdro4 t189;
                                        GM_ST(uint64_t, ((uint8_t *)&t189 + 0), t188);
                                        gm_tftb1odc739t5 t190 = gm_f_write_ho6u1fd(t187, t189);
                                        gm_l64 = t190;
                                        uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                        if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srckg9scnip7e8g, 86, 21);
                                    } else {
                                        void * t192 = gm_l0;
                                        void * t193 = gm_l1;
                                        gm_t88rcucg1b7uk t194 = gm_l19;
                                        gm_b16_8 t195 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t192, t193, t194);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l21 = t195;
                                        void * t196 = (void *)((uint8_t *)&F.l5);
                                        gm_b16_8 t197 = gm_l21;
                                        uint64_t t198 = gm_l20;
                                        uint64_t t199 = gm_l20;
                                        gm_tgbgti82mf879 t200;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 0), t197);
                                        GM_ST(uint64_t, ((uint8_t *)&t200 + 16), t198);
                                        GM_ST(uint64_t, ((uint8_t *)&t200 + 24), t199);
                                        gm_tftb1odc739t5 t201 = gm_f_write_qvj9qj9(t196, &t200);
                                        gm_l65 = t201;
                                        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srckg9scnip7e8g, 89, 21);
                                        void * t203 = (void *)((uint8_t *)&F.l5);
                                        gm_b16_8 t204;
                                        GM_ST(void *, (uint8_t *)&t204, (void *)(gm_shmmkvp83pr05));
                                        GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(17));
                                        gm_tftb1odc739t5 t205 = gm_f_extend_0cs85gs(t203, t204);
                                        gm_l66 = t205;
                                        uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                        if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srckg9scnip7e8g, 90, 21);
                                    }
                                    goto gm_lb11;
                                }
gm_lb19:;
                            }
                            {
                                {
                                    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)13ULL));
                                    if (!t208) {
                                        goto gm_lb20;
                                    }
                                    gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l54) + 8));
                                    gm_l22 = t209;
                                    void * t210 = gm_l0;
                                    void * t211 = gm_l1;
                                    gm_t88rcucg1b7uk t212 = gm_l22;
                                    gm_b16_8 t213 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t210, t211, t212);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l23 = t213;
                                    void * t214 = gm_l1;
                                    gm_t88rcucg1b7uk t215 = gm_l2;
                                    gm_b16_8 t216 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t214, t215);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l67 = t216;
                                    void * t217 = GM_LD(void *, ((uint8_t *)&gm_l67));
                                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t218)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t218, gm_srckg9scnip7e8g, 95, 25);
                                    uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)t217) + (uintptr_t)((uint64_t)1ULL) * 8));
                                    gm_l24 = t219;
                                    void * t220 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t221 = gm_l24;
                                    uint64_t t222 = gm_l24;
                                    gm_t2lqu8jsj511d t223;
                                    GM_ST(uint64_t, ((uint8_t *)&t223 + 0), t221);
                                    GM_ST(uint64_t, ((uint8_t *)&t223 + 8), t222);
                                    gm_tftb1odc739t5 t224 = gm_f_write_jgspi6j(t220, t223);
                                    gm_l68 = t224;
                                    uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                    if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srckg9scnip7e8g, 96, 17);
                                    void * t226 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t227 = gm_l24;
                                    gm_b16_8 t228 = gm_l23;
                                    gm_tf1bjg81u0glv t229;
                                    GM_ST(uint64_t, ((uint8_t *)&t229 + 0), t227);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t229 + 8), t228);
                                    gm_tftb1odc739t5 t230 = gm_f_write_ed06dp1(t226, &t229);
                                    gm_l69 = t230;
                                    uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                    if (!t231) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srckg9scnip7e8g, 97, 17);
                                    goto gm_lb11;
                                }
gm_lb20:;
                                {
                                    uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t233 = (uint8_t)(t232 == ((uint8_t)14ULL));
                                    if (!t233) {
                                        goto gm_lb21;
                                    }
                                    gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l54) + 8));
                                    gm_l25 = t234;
                                    gm_t88rcucg1b7uk t235 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l54) + 8) + 4));
                                    gm_l26 = t235;
                                    void * t236 = gm_l0;
                                    void * t237 = gm_l1;
                                    gm_t88rcucg1b7uk t238 = gm_l25;
                                    gm_b16_8 t239 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t236, t237, t238);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l27 = t239;
                                    void * t240 = gm_l0;
                                    void * t241 = gm_l1;
                                    gm_t88rcucg1b7uk t242 = gm_l26;
                                    gm_b16_8 t243 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t240, t241, t242);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l28 = t243;
                                    void * t244 = gm_l1;
                                    gm_t88rcucg1b7uk t245 = gm_l2;
                                    gm_b16_8 t246 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t244, t245);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l29 = t246;
                                    void * t247 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                    uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t248)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t248, gm_srckg9scnip7e8g, 103, 23);
                                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)t247) + (uintptr_t)((uint64_t)0ULL) * 8));
                                    gm_l30 = t249;
                                    void * t250 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                    uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t251)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t251, gm_srckg9scnip7e8g, 104, 23);
                                    uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)t250) + (uintptr_t)((uint64_t)1ULL) * 8));
                                    gm_l31 = t252;
                                    void * t253 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t254 = gm_l30;
                                    uint64_t t255 = gm_l30;
                                    gm_t2lqu8jsj511d t256;
                                    GM_ST(uint64_t, ((uint8_t *)&t256 + 0), t254);
                                    GM_ST(uint64_t, ((uint8_t *)&t256 + 8), t255);
                                    gm_tftb1odc739t5 t257 = gm_f_write_6nhr3v9(t253, t256);
                                    gm_l70 = t257;
                                    uint8_t t258 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t258) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srckg9scnip7e8g, 105, 17);
                                    void * t259 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t260 = gm_l30;
                                    gm_b16_8 t261 = gm_l27;
                                    uint64_t t262 = gm_l31;
                                    uint64_t t263 = gm_l31;
                                    gm_b16_8 t264 = gm_l28;
                                    uint64_t t265 = gm_l31;
                                    uint64_t t266 = gm_l31;
                                    gm_t6s59i0k0msk8 t267;
                                    GM_ST(uint64_t, ((uint8_t *)&t267 + 0), t260);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t267 + 8), t261);
                                    GM_ST(uint64_t, ((uint8_t *)&t267 + 24), t262);
                                    GM_ST(uint64_t, ((uint8_t *)&t267 + 32), t263);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t267 + 40), t264);
                                    GM_ST(uint64_t, ((uint8_t *)&t267 + 56), t265);
                                    GM_ST(uint64_t, ((uint8_t *)&t267 + 64), t266);
                                    gm_tftb1odc739t5 t268 = gm_f_write_bq1tl8a(t259, &t267);
                                    gm_l71 = t268;
                                    uint8_t t269 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                    if (!t269) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srckg9scnip7e8g, 106, 17);
                                    goto gm_lb11;
                                }
gm_lb21:;
                            }
                            {
                                {
                                    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)17ULL));
                                    if (!t271) {
                                        goto gm_lb22;
                                    }
                                    void * t272 = gm_l1;
                                    gm_t88rcucg1b7uk t273 = gm_l2;
                                    void * t274 = gm_f_adt_of_gej99vn(t272, GM_CONV(gm_csguankko4lmhd, t273));
                                    {
                                        {
                                            uint8_t t275 = GM_LD(uint8_t, ((((uint8_t *)t274) + 24) + 0));
                                            uint8_t t276 = (uint8_t)(t275 == ((uint8_t)0ULL));
                                            if (!t276) {
                                                goto gm_lb24;
                                            }
                                            memmove(((uint8_t *)&F.l32), ((((uint8_t *)t274) + 24) + 8), 48);
                                            {
                                                {
                                                    uint8_t t277 = (uint8_t)(((((uint8_t *)&F.l32) + 40))[4] != 0);
                                                    if (!t277) {
                                                        goto gm_lb26;
                                                    }
                                                    gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 40));
                                                    gm_l33 = t278;
                                                    void * t279 = gm_l0;
                                                    void * t280 = gm_l1;
                                                    gm_t88rcucg1b7uk t281 = gm_l33;
                                                    gm_b16_8 t282 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t279, t280, t281);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l34 = t282;
                                                    void * t283 = (void *)((uint8_t *)&F.l5);
                                                    gm_b16_8 t284 = gm_l34;
                                                    gm_tnpifv20irnph t285;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t285 + 0), t284);
                                                    gm_tftb1odc739t5 t286 = gm_f_write_96p75mj(t283, t285);
                                                    gm_l72 = t286;
                                                    uint8_t t287 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                    if (!t287) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srckg9scnip7e8g, 112, 25);
                                                    goto gm_lb25;
                                                }
gm_lb26:;
                                                void * t288 = gm_l1;
                                                gm_t88rcucg1b7uk t289 = gm_l2;
                                                gm_b16_8 t290 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t288, t289);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l35 = t290;
                                                gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                                gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)&t291));
                                                gm_l36 = t292;
                                                gm_l37 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t293 = gm_l37;
                                                        uint64_t t294 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                        uint8_t t295 = (t293 < t294);
                                                        if (!t295) {
                                                            goto gm_lb27;
                                                        }
                                                        {
                                                            {
                                                                uint64_t t296 = gm_l37;
                                                                void * t297 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                                uint64_t t298 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                                uint64_t t299 = gm_l37;
                                                                gm_t0mglksrkfa5e t300 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t297) + (uintptr_t)t299 * 40));
                                                                GM_ST(uint64_t, ((uint8_t *)&gm_l73 + 0), t296);
                                                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l73 + 8), t300);
                                                            }
                                                            {
                                                                {
                                                                    uint64_t t301 = GM_LD(uint64_t, ((uint8_t *)&gm_l73));
                                                                    gm_l38 = t301;
                                                                    {
                                                                        memmove(((uint8_t *)&F.l39), (((uint8_t *)&gm_l73) + 8), 40);
                                                                    }
                                                                    void * t302 = gm_l0;
                                                                    void * t303 = gm_l1;
                                                                    gm_t88rcucg1b7uk t304 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l39) + 16));
                                                                    gm_b16_8 t305 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t302, t303, t304);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l40 = t305;
                                                                    {
                                                                        void * t306 = (void *)((uint8_t *)&F.l5);
                                                                        gm_b16_8 t307 = gm_l40;
                                                                        void * t308 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                                                        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                                        uint64_t t310 = gm_l38;
                                                                        if ((uintptr_t)t310 >= (uintptr_t)(t309)) gm_trap_index((uintptr_t)t310, t309, gm_srckg9scnip7e8g, 117, 88);
                                                                        uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)t308) + (uintptr_t)t310 * 8));
                                                                        void * t312 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                                                        uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                                        uint64_t t314 = gm_l38;
                                                                        if ((uintptr_t)t314 >= (uintptr_t)(t313)) gm_trap_index((uintptr_t)t314, t313, gm_srckg9scnip7e8g, 117, 97);
                                                                        uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)t312) + (uintptr_t)t314 * 8));
                                                                        gm_tgbgti82mf879 t316;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t316 + 0), t307);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t316 + 16), t311);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t316 + 24), t315);
                                                                        gm_tftb1odc739t5 t317 = gm_f_write_7sl1fre(t306, &t316);
                                                                        gm_l74 = t317;
                                                                    }
                                                                    uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                                                    if (!t318) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srckg9scnip7e8g, 117, 29);
                                                                    goto gm_lb30;
                                                                }
gm_lb31:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 115, 25);
                                                            }
gm_lb30:;
                                                        }
gm_lb29:;
                                                        uint64_t t319 = gm_l37;
                                                        uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                                                        uint64_t t322 = r320;
                                                        gm_l37 = t322;
                                                    }
                                                }
gm_lb27:;
                                                void * t323 = (void *)((uint8_t *)&F.l5);
                                                gm_b16_8 t324;
                                                GM_ST(void *, (uint8_t *)&t324, (void *)(gm_shmmkvp83pr05));
                                                GM_ST(uintptr_t, (uint8_t *)&t324 + 8, (uintptr_t)(17));
                                                gm_tftb1odc739t5 t325 = gm_f_extend_0cs85gs(t323, t324);
                                                gm_l75 = t325;
                                                uint8_t t326 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                                if (!t326) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srckg9scnip7e8g, 119, 25);
                                            }
gm_lb25:;
                                            goto gm_lb23;
                                        }
gm_lb24:;
                                        {
                                            uint8_t t327 = GM_LD(uint8_t, ((((uint8_t *)t274) + 24) + 0));
                                            uint8_t t328 = (uint8_t)(t327 == ((uint8_t)1ULL));
                                            if (!t328) {
                                                goto gm_lb32;
                                            }
                                            gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t274) + 24) + 8));
                                            gm_l41 = t329;
                                            gm_b16_8 t330 = GM_LD(gm_b16_8, (((((uint8_t *)t274) + 24) + 8) + 8));
                                            gm_l42 = t330;
                                            void * t331 = gm_l1;
                                            gm_t88rcucg1b7uk t332 = gm_l2;
                                            gm_b16_8 t333 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t331, t332);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l43 = t333;
                                            void * t334 = gm_l1;
                                            gm_t88rcucg1b7uk t335 = gm_l41;
                                            gm_b16_8 t336 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t334, t335);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l44 = t336;
                                            void * t337 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t338 = gm_l44;
                                            gm_b16_8 t339 = gm_l44;
                                            void * t340 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                            uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t341)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t341, gm_srckg9scnip7e8g, 125, 97);
                                            uint64_t t342 = GM_LD(uint64_t, (((uint8_t *)t340) + (uintptr_t)((uint64_t)0ULL) * 8));
                                            gm_b16_8 t343 = gm_l44;
                                            void * t344 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                            uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t345)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t345, gm_srckg9scnip7e8g, 125, 110);
                                            uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)t344) + (uintptr_t)((uint64_t)0ULL) * 8));
                                            gm_t1h4kdi7gs4v5 t347;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t347 + 0), t338);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t347 + 16), t339);
                                            GM_ST(uint64_t, ((uint8_t *)&t347 + 32), t342);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t347 + 40), t343);
                                            GM_ST(uint64_t, ((uint8_t *)&t347 + 56), t346);
                                            gm_tftb1odc739t5 t348 = gm_f_write_0kurvf5(t337, &t347);
                                            gm_l76 = t348;
                                            uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                            if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srckg9scnip7e8g, 125, 21);
                                            void * t350 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t351;
                                            GM_ST(void *, (uint8_t *)&t351, (void *)(gm_shm5oj4temhh6));
                                            GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)(32));
                                            gm_tftb1odc739t5 t352 = gm_f_extend_0cs85gs(t350, t351);
                                            gm_l77 = t352;
                                            uint8_t t353 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                            if (!t353) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srckg9scnip7e8g, 126, 21);
                                            gm_b16_8 t354 = gm_l42;
                                            gm_l45 = t354;
                                            gm_l46 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t355 = gm_l46;
                                                    uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                    uint8_t t357 = (t355 < t356);
                                                    if (!t357) {
                                                        goto gm_lb33;
                                                    }
                                                    {
                                                        void * t358 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                        uint64_t t360 = gm_l46;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l47), (((uint8_t *)t358) + (uintptr_t)t360 * 72), 72);
                                                                {
                                                                    {
                                                                        uint8_t t361 = (uint8_t)(((((uint8_t *)&F.l47) + 16))[4] != 0);
                                                                        if (!t361) {
                                                                            goto gm_lb39;
                                                                        }
                                                                        gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l47) + 16));
                                                                        gm_l48 = t362;
                                                                        void * t363 = gm_l0;
                                                                        void * t364 = gm_l1;
                                                                        gm_t88rcucg1b7uk t365 = gm_l48;
                                                                        gm_b16_8 t366 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t363, t364, t365);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_l49 = t366;
                                                                        {
                                                                            void * t367 = (void *)((uint8_t *)&F.l5);
                                                                            gm_b16_8 t368 = gm_l44;
                                                                            gm_b16_8 t369 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l47) + 24)));
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&t369));
                                                                            gm_b16_8 t371 = gm_l49;
                                                                            void * t372 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                                                            uint64_t t373 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t373)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t373, gm_srckg9scnip7e8g, 130, 126);
                                                                            uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)t372) + (uintptr_t)((uint64_t)1ULL) * 8));
                                                                            void * t375 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                                                            uint64_t t376 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t376)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t376, gm_srckg9scnip7e8g, 130, 135);
                                                                            uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)t375) + (uintptr_t)((uint64_t)1ULL) * 8));
                                                                            gm_tg23pq9a29gh1 t378;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t378 + 0), t368);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t378 + 16), t370);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t378 + 32), t371);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t378 + 48), t374);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t378 + 56), t377);
                                                                            gm_tftb1odc739t5 t379 = gm_f_write_8ijr07s(t367, &t378);
                                                                            gm_l78 = t379;
                                                                        }
                                                                        uint8_t t380 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                                        if (!t380) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srckg9scnip7e8g, 130, 29);
                                                                        goto gm_lb38;
                                                                    }
gm_lb39:;
                                                                }
gm_lb38:;
                                                                goto gm_lb36;
                                                            }
gm_lb37:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 127, 21);
                                                        }
gm_lb36:;
                                                    }
gm_lb35:;
                                                    uint64_t t381 = gm_l46;
                                                    uint64_t r382; bool o383 = __builtin_add_overflow(t381, ((uint64_t)1ULL), &r382);
                                                    uint64_t t384 = r382;
                                                    gm_l46 = t384;
                                                }
                                            }
gm_lb33:;
                                            void * t385 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t386;
                                            GM_ST(void *, (uint8_t *)&t386, (void *)(gm_shmmkvp83pr05));
                                            GM_ST(uintptr_t, (uint8_t *)&t386 + 8, (uintptr_t)(17));
                                            gm_tftb1odc739t5 t387 = gm_f_extend_0cs85gs(t385, t386);
                                            gm_l79 = t387;
                                            uint8_t t388 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                            if (!t388) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srckg9scnip7e8g, 133, 21);
                                            goto gm_lb23;
                                        }
gm_lb32:;
                                        {
                                            uint8_t t389 = GM_LD(uint8_t, ((((uint8_t *)t274) + 24) + 0));
                                            uint8_t t390 = (uint8_t)(t389 == ((uint8_t)2ULL));
                                            if (!t390) {
                                                goto gm_lb40;
                                            }
                                            void * t391 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t392;
                                            GM_ST(void *, (uint8_t *)&t392, (void *)(gm_sug64ltbhov0m));
                                            GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(18));
                                            gm_tftb1odc739t5 t393 = gm_f_extend_0cs85gs(t391, t392);
                                            gm_l80 = t393;
                                            uint8_t t394 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                            if (!t394) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srckg9scnip7e8g, 135, 31);
                                            goto gm_lb23;
                                        }
gm_lb40:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 108, 27);
                                    }
gm_lb23:;
                                    goto gm_lb11;
                                }
gm_lb22:;
                            }
                            {
                                {
                                    void * t395 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t396;
                                    GM_ST(void *, (uint8_t *)&t396, (void *)(gm_shmmkvp83pr05));
                                    GM_ST(uintptr_t, (uint8_t *)&t396 + 8, (uintptr_t)(17));
                                    gm_tftb1odc739t5 t397 = gm_f_extend_0cs85gs(t395, t396);
                                    gm_l81 = t397;
                                    uint8_t t398 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                    if (!t398) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srckg9scnip7e8g, 137, 18);
                                    goto gm_lb11;
                                }
gm_lb41:;
                                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 61, 14);
                            }
                        }
gm_lb11:;
                    }
                    {
                        goto gm_lb3;
                    }
                }
gm_lb10:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 55, 5);
            }
        }
gm_lb3:;
    }
    {
        void * t399 = gm_l0;
        gm_b16_8 t400 = gm_l4;
        gm_b16_8 t401;
        GM_ST(void *, (uint8_t *)&t401, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t401 + 8, (uintptr_t)(4));
        gm_b16_8 t402;
        GM_ST(void *, (uint8_t *)&t402, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(34));
        gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
        gm_b0_1 t405 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t399, t400, t401, t402, t404);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t406 = gm_l4;
        F.ret = t406;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hash_elems_7gnr39c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_hash_elems_7gnr39c F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_b16_8 gm_l3; /* data */
    gm_b16_8 gm_l4; /* count */
    uint8_t gm_l5; /* signed */
    uint32_t gm_l6; /* bits */
    gm_b16_8 gm_l7; /* step */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t5v8n6qrn4rh4 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        {
            {
                void * t4 = gm_l1;
                gm_t88rcucg1b7uk t5 = gm_l2;
                gm_f_repr_53quqki((void *)&gm_l8, (void *)NULL, cx, NULL, t4, t5);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
            gm_l5 = t8;
            uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&gm_l8) + 8) + 4));
            gm_l6 = t9;
            uint8_t t10 = gm_l5;
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = gm_l6;
            uint8_t t13 = ((t12 == ((uint32_t)8ULL)));
            if (!t13) {
                goto gm_lb2;
            }
            {
                gm_b16_8 t14 = gm_l3;
                gm_b16_8 t15 = gm_l4;
                gm_t6j6smj7qoctu t16;
                GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t14);
                GM_ST(gm_b16_8, ((uint8_t *)&t16 + 16), t15);
                gm_f_format_l1ii34d((void *)&gm_l9, (void *)NULL, cx, NULL, &t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srckg9scnip7e8g, 255, 16);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t20 = gm_l0;
        void * t21 = gm_l1;
        gm_t88rcucg1b7uk t22 = gm_l2;
        gm_b16_8 t23 = gm_l3;
        void * t24 = gm_l1;
        gm_t88rcucg1b7uk t25 = gm_l2;
        uint64_t t26 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3877pk7lv748 t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t23);
        GM_ST(uint64_t, ((uint8_t *)&t27 + 16), t26);
        gm_f_format_eqjc9ro((void *)&gm_l10, (void *)NULL, cx, NULL, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srckg9scnip7e8g, 257, 35);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        gm_b16_8 t31 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t20, t21, t22, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t31;
    }
    {
        gm_b16_8 t32 = gm_l4;
        gm_b16_8 t33 = gm_l7;
        gm_t6j6smj7qoctu t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t32);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 16), t33);
        gm_f_format_8to6k7q((void *)&gm_l11, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srckg9scnip7e8g, 258, 12);
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    F.ret = t37;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_hash_fn_nlk82uj F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* e */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    uint64_t gm_l14; /* len */
    gm_t88rcucg1b7uk gm_l15; /* elem */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_t88rcucg1b7uk gm_l17; /* inner */
    uint64_t gm_l18; /* flag */
    gm_t88rcucg1b7uk gm_l19; /* err */
    gm_t88rcucg1b7uk gm_l20; /* ok */
    gm_b16_8 gm_l21; /* offs */
    gm_b16_8 gm_l22; /* at */
    gm_t88rcucg1b7uk gm_l24; /* u */
    gm_b16_8 gm_l25; /* offs */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_t88rcucg1b7uk gm_l30; /* tag */
    gm_b16_8 gm_l31; /* variants */
    gm_b16_8 gm_l32; /* offs */
    gm_b16_8 gm_l33; /* tc */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    gm_t88rcucg1b7uk gm_l38; /* pt */
    GM_STACK_CHECK(&F, 3440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_t53at80pfdus3 gm_l42;
    gm_tlcm4lvu3g1a8 gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_b16_8 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_t8ta482bl6be0 gm_l58;
    gm_tabb710d9mq2e gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_t3l8idb2m6c1r gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_tftb1odc739t5 gm_l68;
    {
        {
            {
                void * t37 = gm_l0;
                void * t38 = (void *)(((uint8_t *)t37) + 192);
                uint32_t t39 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                gm_f_get_gnmase9((void *)&gm_l39, t38, t39);
                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l39))[16] != 0);
                if (!t40) {
                    goto gm_lb2;
                }
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                gm_l3 = t41;
                gm_b16_8 t42 = gm_l3;
                F.ret = t42;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t43 = gm_l0;
        void * t44 = gm_l1;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 192);
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s9iu5gsqog7uc));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(4));
        gm_t88rcucg1b7uk t48 = gm_l2;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(8));
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(28));
        gm_b16_8 t51 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t43, t44, t46, t47, t48, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t51;
        gm_trqfvmjtt4sij t52;
        gm_f_list_uvfu1k1((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t52;
        void * t53 = gm_l1;
        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
        gm_l6 = t54;
        {
            {
                void * t55 = gm_l1;
                gm_t88rcucg1b7uk t56 = gm_l2;
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_ssdi9g220if8h));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
                gm_f_hash_scalar_pt0pjre((void *)&gm_l40, (void *)NULL, cx, NULL, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                if (!t58) {
                    goto gm_lb4;
                }
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                gm_l7 = t59;
                void * t60 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t61 = gm_l7;
                gm_tnpifv20irnph t62;
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
                gm_tftb1odc739t5 t63 = gm_f_write_5ahb23t(t60, t62);
                gm_l41 = t63;
                uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srckg9scnip7e8g, 268, 9);
                void * t65 = gm_l0;
                gm_b16_8 t66 = gm_l4;
                gm_b16_8 t67;
                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_shn8mjp9kc2n2));
                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(8));
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s81r46mpa8ra9));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(28));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                gm_b0_1 t71 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t65, t66, t67, t68, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t72 = gm_l4;
                F.ret = t72;
                goto gm_fret;
            }
gm_lb4:;
        }
gm_lb3:;
        void * t73 = gm_l1;
        gm_t88rcucg1b7uk t74 = gm_l2;
        gm_f_kind_ghvav22((void *)&gm_l42, t73, GM_CONV(gm_csguankko4lmhd, t74));
        {
            {
                {
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)12ULL));
                    if (!t76) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                    gm_l8 = t77;
                    void * t78 = gm_l1;
                    gm_t88rcucg1b7uk t79 = gm_l2;
                    gm_b16_8 t80 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t78, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t80;
                    gm_b16_8 t81 = gm_l8;
                    gm_l10 = t81;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t82 = gm_l11;
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t84 = (t82 < t83);
                            if (!t84) {
                                goto gm_lb7;
                            }
                            {
                                uint64_t t85 = gm_l11;
                                void * t86 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t88 = gm_l11;
                                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + (uintptr_t)t88 * 4));
                                gm_tlcm4lvu3g1a8 t90;
                                GM_ST(uint32_t, (uint8_t *)&t90 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t85);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t90 + 8), t89);
                                gm_l43 = t90;
                                {
                                    {
                                        uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                                        gm_l12 = t91;
                                        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 8));
                                        gm_l13 = t92;
                                        void * t93 = (void *)((uint8_t *)&F.l5);
                                        void * t94 = gm_l0;
                                        void * t95 = gm_l1;
                                        gm_t88rcucg1b7uk t96 = gm_l13;
                                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t99 = gm_l12;
                                        if ((uintptr_t)t99 >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)t99, t98, gm_srckg9scnip7e8g, 276, 78);
                                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t97) + (uintptr_t)t99 * 8));
                                        gm_t4ce0jd3tdro4 t101;
                                        GM_ST(uint64_t, ((uint8_t *)&t101 + 0), t100);
                                        gm_f_format_g3tqbla((void *)&gm_l44, (void *)NULL, cx, NULL, t101);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                        if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srckg9scnip7e8g, 276, 57);
                                        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                                        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                                        gm_b16_8 t105 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t94, t95, t96, t104);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t106;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                                        gm_tftb1odc739t5 t107 = gm_f_write_aaqg4k2(t93, t106);
                                        gm_l45 = t107;
                                        uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srckg9scnip7e8g, 276, 17);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 275, 13);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t109 = gm_l11;
                            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                            uint64_t t112 = r110;
                            gm_l11 = t112;
                        }
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t114 = (uint8_t)(t113 == ((uint8_t)11ULL));
                    if (!t114) {
                        goto gm_lb12;
                    }
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                    gm_l14 = t115;
                    gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l42) + 8) + 8));
                    gm_l15 = t116;
                    void * t117 = (void *)((uint8_t *)&F.l5);
                    void * t118 = gm_l0;
                    void * t119 = gm_l1;
                    gm_t88rcucg1b7uk t120 = gm_l15;
                    gm_b16_8 t121;
                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_ssdi9g220if8h));
                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(1));
                    uint64_t t122 = gm_l14;
                    gm_t4ce0jd3tdro4 t123;
                    GM_ST(uint64_t, ((uint8_t *)&t123 + 0), t122);
                    gm_f_format_1amnh72((void *)&gm_l46, (void *)NULL, cx, NULL, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srckg9scnip7e8g, 279, 70);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                    gm_b16_8 t127 = gm_f_hash_elems_7gnr39c((void *)NULL, cx, NULL, t118, t119, t120, t121, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t128 = gm_f_extend_0cs85gs(t117, t127);
                    gm_l47 = t128;
                    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srckg9scnip7e8g, 279, 33);
                    goto gm_lb5;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)10ULL));
                    if (!t131) {
                        goto gm_lb13;
                    }
                    gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l42) + 8) + 4));
                    gm_l16 = t132;
                    void * t133 = (void *)((uint8_t *)&F.l5);
                    uint64_t t134 = gm_l6;
                    gm_t4ce0jd3tdro4 t135;
                    GM_ST(uint64_t, ((uint8_t *)&t135 + 0), t134);
                    gm_tftb1odc739t5 t136 = gm_f_write_k9lbjqb(t133, t135);
                    gm_l48 = t136;
                    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srckg9scnip7e8g, 281, 13);
                    void * t138 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t139;
                    GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sfm0hseeqersg));
                    GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(24));
                    gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t138, t139);
                    gm_l49 = t140;
                    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                    if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srckg9scnip7e8g, 282, 13);
                    void * t142 = (void *)((uint8_t *)&F.l5);
                    void * t143 = gm_l0;
                    void * t144 = gm_l1;
                    gm_t88rcucg1b7uk t145 = gm_l16;
                    gm_b16_8 t146;
                    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_stafskn6bv24f));
                    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(1));
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_smnfu5n5pum1l));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(1));
                    gm_b16_8 t148 = gm_f_hash_elems_7gnr39c((void *)NULL, cx, NULL, t143, t144, t145, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t142, t148);
                    gm_l50 = t149;
                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srckg9scnip7e8g, 283, 13);
                    goto gm_lb5;
                }
gm_lb13:;
                {
                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)13ULL));
                    if (!t152) {
                        goto gm_lb14;
                    }
                    gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 8));
                    gm_l17 = t153;
                    void * t154 = gm_l1;
                    gm_t88rcucg1b7uk t155 = gm_l2;
                    gm_b16_8 t156 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t154, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l51 = t156;
                    void * t157 = GM_LD(void *, ((uint8_t *)&gm_l51));
                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t158, gm_srckg9scnip7e8g, 286, 21);
                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)t157) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_l18 = t159;
                    void * t160 = (void *)((uint8_t *)&F.l5);
                    uint64_t t161 = gm_l18;
                    gm_t4ce0jd3tdro4 t162;
                    GM_ST(uint64_t, ((uint8_t *)&t162 + 0), t161);
                    gm_tftb1odc739t5 t163 = gm_f_write_4unpkmv(t160, t162);
                    gm_l52 = t163;
                    uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                    if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srckg9scnip7e8g, 287, 13);
                    void * t165 = (void *)((uint8_t *)&F.l5);
                    void * t166 = gm_l0;
                    void * t167 = gm_l1;
                    gm_t88rcucg1b7uk t168 = gm_l17;
                    gm_b16_8 t169;
                    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_ssdi9g220if8h));
                    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(1));
                    gm_b16_8 t170 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t166, t167, t168, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t171;
                    GM_ST(gm_b16_8, ((uint8_t *)&t171 + 0), t170);
                    gm_tftb1odc739t5 t172 = gm_f_write_jfl63ic(t165, t171);
                    gm_l53 = t172;
                    uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srckg9scnip7e8g, 288, 13);
                    goto gm_lb5;
                }
gm_lb14:;
                {
                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)14ULL));
                    if (!t175) {
                        goto gm_lb15;
                    }
                    gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 8));
                    gm_l19 = t176;
                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l42) + 8) + 4));
                    gm_l20 = t177;
                    void * t178 = gm_l1;
                    gm_t88rcucg1b7uk t179 = gm_l2;
                    gm_b16_8 t180 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t178, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t180;
                    void * t181 = (void *)((uint8_t *)&F.l5);
                    void * t182 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t183)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t183, gm_srckg9scnip7e8g, 292, 74);
                    uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)t182) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_t4ce0jd3tdro4 t185;
                    GM_ST(uint64_t, ((uint8_t *)&t185 + 0), t184);
                    gm_tftb1odc739t5 t186 = gm_f_write_7k4ript(t181, t185);
                    gm_l54 = t186;
                    uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srckg9scnip7e8g, 292, 13);
                    {
                        void * t188 = GM_LD(void *, ((uint8_t *)&gm_l21));
                        uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t189)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t189, gm_srckg9scnip7e8g, 293, 40);
                        uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)t188) + (uintptr_t)((uint64_t)1ULL) * 8));
                        gm_t4ce0jd3tdro4 t191;
                        GM_ST(uint64_t, ((uint8_t *)&t191 + 0), t190);
                        gm_f_format_g3tqbla((void *)&gm_l55, (void *)NULL, cx, NULL, t191);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                    if (!t192) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_srckg9scnip7e8g, 293, 19);
                    gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                    gm_l22 = t193;
                    {
                        void * t194 = (void *)((uint8_t *)&F.l5);
                        void * t195 = gm_l0;
                        void * t196 = gm_l1;
                        gm_t88rcucg1b7uk t197 = gm_l19;
                        gm_b16_8 t198 = gm_l22;
                        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                        gm_b16_8 t200 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t195, t196, t197, t199);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t201 = gm_l0;
                        void * t202 = gm_l1;
                        gm_t88rcucg1b7uk t203 = gm_l20;
                        gm_b16_8 t204 = gm_l22;
                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                        gm_b16_8 t206 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t201, t202, t203, t205);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t207;
                        GM_ST(gm_b16_8, ((uint8_t *)&t207 + 0), t200);
                        GM_ST(gm_b16_8, ((uint8_t *)&t207 + 16), t206);
                        gm_tftb1odc739t5 t208 = gm_f_write_ltl9pvt(t194, &t207);
                        gm_l56 = t208;
                    }
                    uint8_t t209 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                    if (!t209) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srckg9scnip7e8g, 294, 13);
                    goto gm_lb5;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                    uint8_t t211 = (uint8_t)(t210 == ((uint8_t)17ULL));
                    if (!t211) {
                        goto gm_lb16;
                    }
                    void * t212 = gm_l1;
                    gm_t88rcucg1b7uk t213 = gm_l2;
                    void * t214 = gm_f_adt_of_gej99vn(t212, GM_CONV(gm_csguankko4lmhd, t213));
                    {
                        {
                            uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t214) + 24) + 0));
                            uint8_t t216 = (uint8_t)(t215 == ((uint8_t)0ULL));
                            if (!t216) {
                                goto gm_lb18;
                            }
                            memmove(((uint8_t *)&F.l23), ((((uint8_t *)t214) + 24) + 8), 48);
                            {
                                {
                                    uint8_t t217 = (uint8_t)(((((uint8_t *)&F.l23) + 40))[4] != 0);
                                    if (!t217) {
                                        goto gm_lb20;
                                    }
                                    gm_t88rcucg1b7uk t218 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l23) + 40));
                                    gm_l24 = t218;
                                    void * t219 = (void *)((uint8_t *)&F.l5);
                                    void * t220 = gm_l0;
                                    void * t221 = gm_l1;
                                    gm_t88rcucg1b7uk t222 = gm_l24;
                                    gm_b16_8 t223;
                                    GM_ST(void *, (uint8_t *)&t223, (void *)(gm_ssdi9g220if8h));
                                    GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(1));
                                    gm_b16_8 t224 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t220, t221, t222, t223);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t225;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t225 + 0), t224);
                                    gm_tftb1odc739t5 t226 = gm_f_write_aaqg4k2(t219, t225);
                                    gm_l57 = t226;
                                    uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                    if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srckg9scnip7e8g, 299, 21);
                                    goto gm_lb19;
                                }
gm_lb20:;
                                void * t228 = gm_l1;
                                gm_t88rcucg1b7uk t229 = gm_l2;
                                gm_b16_8 t230 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t228, t229);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l25 = t230;
                                gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                                gm_l26 = t232;
                                gm_l27 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t233 = gm_l27;
                                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                        uint8_t t235 = (t233 < t234);
                                        if (!t235) {
                                            goto gm_lb21;
                                        }
                                        {
                                            {
                                                uint64_t t236 = gm_l27;
                                                void * t237 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                                uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                uint64_t t239 = gm_l27;
                                                gm_t0mglksrkfa5e t240 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t237) + (uintptr_t)t239 * 40));
                                                GM_ST(uint64_t, ((uint8_t *)&gm_l58 + 0), t236);
                                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l58 + 8), t240);
                                            }
                                            {
                                                {
                                                    uint64_t t241 = GM_LD(uint64_t, ((uint8_t *)&gm_l58));
                                                    gm_l28 = t241;
                                                    {
                                                        memmove(((uint8_t *)&F.l29), (((uint8_t *)&gm_l58) + 8), 40);
                                                    }
                                                    {
                                                        void * t242 = (void *)((uint8_t *)&F.l5);
                                                        void * t243 = gm_l0;
                                                        void * t244 = gm_l1;
                                                        gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 16));
                                                        void * t246 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                                        uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                                        uint64_t t248 = gm_l28;
                                                        if ((uintptr_t)t248 >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)t248, t247, gm_srckg9scnip7e8g, 303, 89);
                                                        uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)t246) + (uintptr_t)t248 * 8));
                                                        gm_t4ce0jd3tdro4 t250;
                                                        GM_ST(uint64_t, ((uint8_t *)&t250 + 0), t249);
                                                        gm_f_format_g3tqbla((void *)&gm_l59, (void *)NULL, cx, NULL, t250);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                                        if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_srckg9scnip7e8g, 303, 68);
                                                        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                                                        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
                                                        gm_b16_8 t254 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t243, t244, t245, t253);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tnpifv20irnph t255;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                                                        gm_tftb1odc739t5 t256 = gm_f_write_aaqg4k2(t242, t255);
                                                        gm_l60 = t256;
                                                    }
                                                    uint8_t t257 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                    if (!t257) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srckg9scnip7e8g, 303, 25);
                                                    goto gm_lb24;
                                                }
gm_lb25:;
                                                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 302, 21);
                                            }
gm_lb24:;
                                        }
gm_lb23:;
                                        uint64_t t258 = gm_l27;
                                        uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                                        uint64_t t261 = r259;
                                        gm_l27 = t261;
                                    }
                                }
gm_lb21:;
                            }
gm_lb19:;
                            goto gm_lb17;
                        }
gm_lb18:;
                        {
                            uint8_t t262 = GM_LD(uint8_t, ((((uint8_t *)t214) + 24) + 0));
                            uint8_t t263 = (uint8_t)(t262 == ((uint8_t)1ULL));
                            if (!t263) {
                                goto gm_lb26;
                            }
                            gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t214) + 24) + 8));
                            gm_l30 = t264;
                            gm_b16_8 t265 = GM_LD(gm_b16_8, (((((uint8_t *)t214) + 24) + 8) + 8));
                            gm_l31 = t265;
                            void * t266 = gm_l1;
                            gm_t88rcucg1b7uk t267 = gm_l2;
                            gm_b16_8 t268 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t266, t267);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l32 = t268;
                            void * t269 = gm_l1;
                            gm_t88rcucg1b7uk t270 = gm_l30;
                            gm_b16_8 t271 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t269, t270);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l33 = t271;
                            void * t272 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t273 = gm_l33;
                            gm_b16_8 t274 = gm_l33;
                            void * t275 = GM_LD(void *, ((uint8_t *)&gm_l32));
                            uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t276)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t276, gm_srckg9scnip7e8g, 310, 69);
                            uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)t275) + (uintptr_t)((uint64_t)0ULL) * 8));
                            gm_tftksr4op114e t278;
                            GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t273);
                            GM_ST(gm_b16_8, ((uint8_t *)&t278 + 16), t274);
                            GM_ST(uint64_t, ((uint8_t *)&t278 + 32), t277);
                            gm_tftb1odc739t5 t279 = gm_f_write_551e4in(t272, &t278);
                            gm_l61 = t279;
                            uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srckg9scnip7e8g, 310, 17);
                            gm_b16_8 t281 = gm_l31;
                            gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                            gm_l34 = t282;
                            gm_l35 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t283 = gm_l35;
                                    uint64_t t284 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                    uint8_t t285 = (t283 < t284);
                                    if (!t285) {
                                        goto gm_lb27;
                                    }
                                    {
                                        {
                                            uint64_t t286 = gm_l35;
                                            void * t287 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                            uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                            uint64_t t289 = gm_l35;
                                            gm_tckdobunaulee t290 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t287) + (uintptr_t)t289 * 72));
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l62 + 0), t286);
                                            GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l62 + 8), t290);
                                        }
                                        {
                                            {
                                                uint64_t t291 = GM_LD(uint64_t, ((uint8_t *)&gm_l62));
                                                gm_l36 = t291;
                                                {
                                                    memmove(((uint8_t *)&F.l37), (((uint8_t *)&gm_l62) + 8), 72);
                                                }
                                                {
                                                    void * t292 = (void *)((uint8_t *)&F.l5);
                                                    gm_b16_8 t293 = gm_l33;
                                                    gm_b16_8 t294 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l37) + 24)));
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                                                    gm_t6j6smj7qoctu t296;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t296 + 0), t293);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t296 + 16), t295);
                                                    gm_tftb1odc739t5 t297 = gm_f_write_2tci68e(t292, &t296);
                                                    gm_l63 = t297;
                                                }
                                                uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                                if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srckg9scnip7e8g, 312, 21);
                                                void * t299 = (void *)((uint8_t *)&F.l5);
                                                uint64_t t300 = gm_l36;
                                                gm_t4ce0jd3tdro4 t301;
                                                GM_ST(uint64_t, ((uint8_t *)&t301 + 0), t300);
                                                gm_tftb1odc739t5 t302 = gm_f_write_bjaiklv(t299, t301);
                                                gm_l64 = t302;
                                                uint8_t t303 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                if (!t303) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srckg9scnip7e8g, 313, 21);
                                                {
                                                    {
                                                        uint8_t t304 = (uint8_t)(((((uint8_t *)&F.l37) + 16))[4] != 0);
                                                        if (!t304) {
                                                            goto gm_lb33;
                                                        }
                                                        gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l37) + 16));
                                                        gm_l38 = t305;
                                                        void * t306 = (void *)((uint8_t *)&F.l5);
                                                        void * t307 = gm_l0;
                                                        void * t308 = gm_l1;
                                                        gm_t88rcucg1b7uk t309 = gm_l38;
                                                        void * t310 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                        uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t311)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t311, gm_srckg9scnip7e8g, 315, 90);
                                                        uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)t310) + (uintptr_t)((uint64_t)1ULL) * 8));
                                                        gm_t4ce0jd3tdro4 t313;
                                                        GM_ST(uint64_t, ((uint8_t *)&t313 + 0), t312);
                                                        gm_f_format_g3tqbla((void *)&gm_l65, (void *)NULL, cx, NULL, t313);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t314 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                        if (!t314) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srckg9scnip7e8g, 315, 69);
                                                        gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                                        gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
                                                        gm_b16_8 t317 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t307, t308, t309, t316);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tnpifv20irnph t318;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t318 + 0), t317);
                                                        gm_tftb1odc739t5 t319 = gm_f_write_q1ni37m(t306, t318);
                                                        gm_l66 = t319;
                                                        uint8_t t320 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                        if (!t320) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srckg9scnip7e8g, 315, 25);
                                                        goto gm_lb32;
                                                    }
gm_lb33:;
                                                }
gm_lb32:;
                                                void * t321 = (void *)((uint8_t *)&F.l5);
                                                gm_b16_8 t322;
                                                GM_ST(void *, (uint8_t *)&t322, (void *)(gm_sciq0a78hirud));
                                                GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(24));
                                                gm_tftb1odc739t5 t323 = gm_f_extend_0cs85gs(t321, t322);
                                                gm_l67 = t323;
                                                uint8_t t324 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                if (!t324) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srckg9scnip7e8g, 317, 21);
                                                goto gm_lb30;
                                            }
gm_lb31:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 311, 17);
                                        }
gm_lb30:;
                                    }
gm_lb29:;
                                    uint64_t t325 = gm_l35;
                                    uint64_t r326; bool o327 = __builtin_add_overflow(t325, ((uint64_t)1ULL), &r326);
                                    uint64_t t328 = r326;
                                    gm_l35 = t328;
                                }
                            }
gm_lb27:;
                            goto gm_lb17;
                        }
gm_lb26:;
                        {
                            uint8_t t329 = GM_LD(uint8_t, ((((uint8_t *)t214) + 24) + 0));
                            uint8_t t330 = (uint8_t)(t329 == ((uint8_t)2ULL));
                            if (!t330) {
                                goto gm_lb34;
                            }
                            goto gm_lb17;
                        }
gm_lb34:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 296, 23);
                    }
gm_lb17:;
                    goto gm_lb5;
                }
gm_lb16:;
            }
            {
                {
                    goto gm_lb5;
                }
gm_lb35:;
                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 272, 5);
            }
        }
gm_lb5:;
    }
    {
        void * t331 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t332;
        GM_ST(void *, (uint8_t *)&t332, (void *)(gm_sel1fj24qk32p));
        GM_ST(uintptr_t, (uint8_t *)&t332 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t333 = gm_f_extend_0cs85gs(t331, t332);
        gm_l68 = t333;
        uint8_t t334 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
        if (!t334) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srckg9scnip7e8g, 324, 5);
        void * t335 = gm_l0;
        gm_b16_8 t336 = gm_l4;
        gm_b16_8 t337;
        GM_ST(void *, (uint8_t *)&t337, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(8));
        gm_b16_8 t338;
        GM_ST(void *, (uint8_t *)&t338, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(28));
        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
        gm_b0_1 t341 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t335, t336, t337, t338, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t342 = gm_l4;
        F.ret = t342;
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
inline __attribute__((aligned(4))) void gm_f_hash_scalar_pt0pjre(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_hash_scalar_pt0pjre F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* at */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* storage */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* wide */
    GM_STACK_CHECK(&F, 777u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t5v8n6qrn4rh4 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l7, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t12;
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)&gm_l7) + 8) + 8));
            gm_l4 = t13;
            void * t14 = gm_l0;
            gm_t88rcucg1b7uk t15 = gm_l1;
            gm_b16_8 t16 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t16;
            uint64_t t17 = gm_l4;
            uint8_t t18 = (t17 > ((uint64_t)8ULL));
            if (t18) {
                {
                    gm_b16_8 t19 = gm_l5;
                    gm_b16_8 t20 = gm_l2;
                    gm_t6j6smj7qoctu t21;
                    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t19);
                    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 16), t20);
                    gm_f_format_5f2vpdc((void *)&gm_l8, (void *)NULL, cx, NULL, &t21);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srckg9scnip7e8g, 234, 37);
                {
                    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t24);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    goto gm_fdone;
                }
            }
            uint8_t t25 = gm_l3;
            if (t25) {
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sl6pi8la5phb6));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(19));
                gm_l6 = t26;
            } else {
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_slq0l63qf1dvs));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(10));
                gm_l6 = t27;
            }
            {
                gm_b16_8 t28 = gm_l6;
                gm_b16_8 t29 = gm_l5;
                gm_b16_8 t30 = gm_l2;
                gm_tfeab279c16rr t31;
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t28);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t29);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 32), t30);
                gm_f_format_lovbvbi((void *)&gm_l9, (void *)NULL, cx, NULL, &t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srckg9scnip7e8g, 236, 20);
            {
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t34);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
            if (!t36) {
                goto gm_lb3;
            }
            {
                void * t37 = gm_l0;
                gm_t88rcucg1b7uk t38 = gm_l1;
                gm_b16_8 t39 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t40 = gm_l2;
                gm_t6j6smj7qoctu t41;
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t39);
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 16), t40);
                gm_f_format_4hhihlj((void *)&gm_l10, (void *)NULL, cx, NULL, &t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srckg9scnip7e8g, 238, 29);
            {
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t44);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
            if (!t46) {
                goto gm_lb4;
            }
            {
                gm_b16_8 t47 = gm_l2;
                gm_tnpifv20irnph t48;
                GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                gm_f_format_me0dr2l((void *)&gm_l11, (void *)NULL, cx, NULL, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srckg9scnip7e8g, 239, 25);
            {
                gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t51);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)3ULL));
            if (!t53) {
                goto gm_lb5;
            }
            {
                gm_b16_8 t54 = gm_l2;
                gm_tnpifv20irnph t55;
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                gm_f_format_vd1blml((void *)&gm_l12, (void *)NULL, cx, NULL, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srckg9scnip7e8g, 240, 24);
            {
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t58);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb5:;
        {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 231, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hash_step_ari7fns(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_hash_step_ari7fns F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* at */
    gm_b16_8 gm_l4; /* e */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        {
            {
                void * t4 = gm_l1;
                gm_t88rcucg1b7uk t5 = gm_l2;
                gm_b16_8 t6 = gm_l3;
                gm_f_hash_scalar_pt0pjre((void *)&gm_l5, (void *)NULL, cx, NULL, t4, t5, t6);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l4 = t8;
            {
                gm_b16_8 t9 = gm_l4;
                gm_tnpifv20irnph t10;
                GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
                gm_f_format_8nj2557((void *)&gm_l6, (void *)NULL, cx, NULL, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srckg9scnip7e8g, 247, 45);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            F.ret = t13;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_b16_8 t17 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t18 = gm_l3;
        gm_t6j6smj7qoctu t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t17);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t18);
        gm_f_format_edav5jt((void *)&gm_l7, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srckg9scnip7e8g, 248, 12);
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_helper_name_fob48de F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_b16_8 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 248);
    gm_b16_8 t4 = gm_l2;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_39snre1((void *)&gm_l4, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srckg9scnip7e8g, 28, 105);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    void * t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = gm_l3;
    gm_b16_8 t11 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t12 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t3, t8, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t12;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_less_fn_64um0ab F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* fe */
    gm_b16_8 gm_l15; /* fl */
    uint64_t gm_l16; /* o */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_tf5srh4mp4b3i gm_l18; /* array_len */
    uint64_t gm_l19; /* len */
    gm_t88rcucg1b7uk gm_l20; /* el */
    gm_t88rcucg1b7uk gm_l21; /* el */
    uint64_t gm_l22; /* esz */
    uint64_t gm_l23; /* n */
    gm_b16_8 gm_l24; /* fe */
    gm_b16_8 gm_l25; /* fl */
    gm_t88rcucg1b7uk gm_l26; /* tag */
    gm_b16_8 gm_l27; /* tc */
    uint64_t gm_l28; /* o */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_b16_8 gm_l31; /* f */
    GM_STACK_CHECK(&F, 2417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l32;
    gm_t5v8n6qrn4rh4 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_t53at80pfdus3 gm_l35;
    gm_tlcm4lvu3g1a8 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_t53at80pfdus3 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_b16_8 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    {
        {
            {
                void * t37 = gm_l0;
                void * t38 = (void *)(((uint8_t *)t37) + 136);
                uint32_t t39 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                gm_f_get_gnmase9((void *)&gm_l32, t38, t39);
                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                if (!t40) {
                    goto gm_lb2;
                }
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l32));
                gm_l3 = t41;
                gm_b16_8 t42 = gm_l3;
                F.ret = t42;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t43 = gm_l0;
        void * t44 = gm_l1;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 136);
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_surrjo1qo53mh));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t48 = gm_l2;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(4));
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(34));
        gm_b16_8 t51 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t43, t44, t46, t47, t48, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t51;
        gm_trqfvmjtt4sij t52;
        gm_f_list_uvfu1k1((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t52;
        void * t53 = gm_l1;
        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
        gm_l6 = t54;
        void * t55 = gm_l1;
        gm_t88rcucg1b7uk t56 = gm_l2;
        gm_f_repr_53quqki((void *)&gm_l33, (void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                {
                    {
                        {
                            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                            if (!t58) {
                                goto gm_lb6;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)1ULL));
                            if (!t60) {
                                goto gm_lb7;
                            }
                            goto gm_lb5;
                        }
gm_lb7:;
                        {
                            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)3ULL));
                            if (!t62) {
                                goto gm_lb8;
                            }
                            goto gm_lb5;
                        }
gm_lb8:;
                        {
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)2ULL));
                            if (!t64) {
                                goto gm_lb9;
                            }
                            goto gm_lb5;
                        }
gm_lb9:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    void * t65 = gm_l1;
                    gm_t88rcucg1b7uk t66 = gm_l2;
                    gm_b16_8 t67 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t67;
                    void * t68 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t69 = gm_l7;
                    gm_b16_8 t70 = gm_l7;
                    gm_t6j6smj7qoctu t71;
                    GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
                    GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
                    gm_tftb1odc739t5 t72 = gm_f_write_ikd2l7g(t68, &t71);
                    gm_l34 = t72;
                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srckg9scnip7e8g, 160, 13);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        void * t74 = gm_l1;
                        gm_t88rcucg1b7uk t75 = gm_l2;
                        gm_f_kind_ghvav22((void *)&gm_l35, t74, GM_CONV(gm_csguankko4lmhd, t75));
                        {
                            {
                                {
                                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)12ULL));
                                    if (!t77) {
                                        goto gm_lb12;
                                    }
                                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                    gm_l8 = t78;
                                    void * t79 = gm_l1;
                                    gm_t88rcucg1b7uk t80 = gm_l2;
                                    gm_b16_8 t81 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t79, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l9 = t81;
                                    gm_b16_8 t82 = gm_l8;
                                    gm_l10 = t82;
                                    gm_l11 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t83 = gm_l11;
                                            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint8_t t85 = (t83 < t84);
                                            if (!t85) {
                                                goto gm_lb13;
                                            }
                                            {
                                                uint64_t t86 = gm_l11;
                                                void * t87 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t89 = gm_l11;
                                                gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + (uintptr_t)t89 * 4));
                                                gm_tlcm4lvu3g1a8 t91;
                                                GM_ST(uint32_t, (uint8_t *)&t91 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t91 + 0), t86);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t91 + 8), t90);
                                                gm_l36 = t91;
                                                {
                                                    {
                                                        uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                                        gm_l12 = t92;
                                                        gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                                                        gm_l13 = t93;
                                                        void * t94 = gm_l0;
                                                        void * t95 = gm_l1;
                                                        gm_t88rcucg1b7uk t96 = gm_l13;
                                                        gm_b16_8 t97 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t94, t95, t96);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l14 = t97;
                                                        void * t98 = gm_l0;
                                                        void * t99 = gm_l1;
                                                        gm_t88rcucg1b7uk t100 = gm_l13;
                                                        gm_b16_8 t101 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t98, t99, t100);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l15 = t101;
                                                        void * t102 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                        uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                        uint64_t t104 = gm_l12;
                                                        if ((uintptr_t)t104 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t104, t103, gm_srckg9scnip7e8g, 168, 26);
                                                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)t102) + (uintptr_t)t104 * 8));
                                                        gm_l16 = t105;
                                                        void * t106 = (void *)((uint8_t *)&F.l5);
                                                        gm_b16_8 t107 = gm_l14;
                                                        uint64_t t108 = gm_l16;
                                                        uint64_t t109 = gm_l16;
                                                        gm_b16_8 t110 = gm_l15;
                                                        uint64_t t111 = gm_l16;
                                                        uint64_t t112 = gm_l16;
                                                        gm_tr4ummvbupakb t113;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t107);
                                                        GM_ST(uint64_t, ((uint8_t *)&t113 + 16), t108);
                                                        GM_ST(uint64_t, ((uint8_t *)&t113 + 24), t109);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 32), t110);
                                                        GM_ST(uint64_t, ((uint8_t *)&t113 + 48), t111);
                                                        GM_ST(uint64_t, ((uint8_t *)&t113 + 56), t112);
                                                        gm_tftb1odc739t5 t114 = gm_f_write_e15chue(t106, &t113);
                                                        gm_l37 = t114;
                                                        uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                        if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srckg9scnip7e8g, 169, 21);
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 165, 17);
                                                }
gm_lb16:;
                                            }
gm_lb15:;
                                            uint64_t t116 = gm_l11;
                                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                                            uint64_t t119 = r117;
                                            gm_l11 = t119;
                                        }
                                    }
gm_lb13:;
                                    void * t120 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t121;
                                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sug64ltbhov0m));
                                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(18));
                                    gm_tftb1odc739t5 t122 = gm_f_extend_0cs85gs(t120, t121);
                                    gm_l38 = t122;
                                    uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                    if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srckg9scnip7e8g, 171, 17);
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
                            {
                                {
                                    {
                                        {
                                            uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                                            uint8_t t125 = (uint8_t)(t124 == ((uint8_t)11ULL));
                                            if (!t125) {
                                                goto gm_lb20;
                                            }
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                        {
                                            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                                            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)10ULL));
                                            if (!t127) {
                                                goto gm_lb21;
                                            }
                                            goto gm_lb19;
                                        }
gm_lb21:;
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    gm_t88rcucg1b7uk t128 = gm_l2;
                                    gm_l17 = t128;
                                    gm_tf5srh4mp4b3i t129;
                                    memset(&t129, 0, sizeof t129);
                                    gm_l18 = t129;
                                    {
                                        void * t130 = gm_l1;
                                        gm_t88rcucg1b7uk t131 = gm_l2;
                                        gm_f_kind_ghvav22((void *)&gm_l39, t130, GM_CONV(gm_csguankko4lmhd, t131));
                                    }
                                    {
                                        {
                                            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
                                            uint8_t t133 = (uint8_t)(t132 == ((uint8_t)11ULL));
                                            if (!t133) {
                                                goto gm_lb23;
                                            }
                                            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                            gm_l19 = t134;
                                            gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l39) + 8) + 8));
                                            gm_l20 = t135;
                                            gm_t88rcucg1b7uk t136 = gm_l20;
                                            gm_l17 = t136;
                                            uint64_t t137 = gm_l19;
                                            gm_tf5srh4mp4b3i t138;
                                            GM_ST(uint8_t, (uint8_t *)&t138 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t138 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t138 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t138), t137);
                                            ((uint8_t *)&t138)[8] = 1;
                                            gm_l18 = t138;
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        {
                                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
                                            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)10ULL));
                                            if (!t140) {
                                                goto gm_lb24;
                                            }
                                            gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l39) + 8) + 4));
                                            gm_l21 = t141;
                                            gm_t88rcucg1b7uk t142 = gm_l21;
                                            gm_l17 = t142;
                                            goto gm_lb22;
                                        }
gm_lb24:;
                                        {
                                            gm_trap("reached `@unreachable()`", gm_srckg9scnip7e8g, 182, 26);
                                        }
gm_lb25:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 176, 17);
                                    }
gm_lb22:;
                                    void * t143 = gm_l1;
                                    gm_t88rcucg1b7uk t144 = gm_l17;
                                    uint64_t t145 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t143, t144);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l22 = t145;
                                    {
                                        {
                                            uint8_t t146 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
                                            if (!t146) {
                                                goto gm_lb27;
                                            }
                                            uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                                            gm_l23 = t147;
                                            void * t148 = (void *)((uint8_t *)&F.l5);
                                            uint64_t t149 = gm_l23;
                                            uint64_t t150 = gm_l23;
                                            gm_t2lqu8jsj511d t151;
                                            GM_ST(uint64_t, ((uint8_t *)&t151 + 0), t149);
                                            GM_ST(uint64_t, ((uint8_t *)&t151 + 8), t150);
                                            gm_tftb1odc739t5 t152 = gm_f_write_8tfqc0p(t148, t151);
                                            gm_l40 = t152;
                                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srckg9scnip7e8g, 186, 21);
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                        void * t154 = (void *)((uint8_t *)&F.l5);
                                        uint64_t t155 = gm_l6;
                                        uint64_t t156 = gm_l6;
                                        gm_t2lqu8jsj511d t157;
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 0), t155);
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 8), t156);
                                        gm_tftb1odc739t5 t158 = gm_f_write_k88klp2(t154, t157);
                                        gm_l41 = t158;
                                        uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srckg9scnip7e8g, 188, 21);
                                        void * t160 = (void *)((uint8_t *)&F.l5);
                                        gm_b16_8 t161;
                                        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sor8flejv9lfq));
                                        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(66));
                                        gm_tftb1odc739t5 t162 = gm_f_extend_0cs85gs(t160, t161);
                                        gm_l42 = t162;
                                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                        if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srckg9scnip7e8g, 189, 21);
                                    }
gm_lb26:;
                                    {
                                        void * t164 = gm_l1;
                                        gm_t88rcucg1b7uk t165 = gm_l17;
                                        gm_t5v8n6qrn4rh4 t166;
                                        gm_f_repr_53quqki((void *)&t166, (void *)NULL, cx, NULL, t164, t165);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tuglhjjmj2g58 t167;
                                        GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)0));
                                        GM_ST(uint32_t, ((uint8_t *)&t167 + 4), ((uint32_t)8ULL));
                                        GM_ST(uint64_t, ((uint8_t *)&t167 + 8), ((uint64_t)1ULL));
                                        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&t166) + 0));
                                        uint8_t t169 = (uint8_t)(t168 == ((uint8_t)0ULL));
                                        if (t169) {
                                            gm_tuglhjjmj2g58 t170 = GM_LD(gm_tuglhjjmj2g58, (((uint8_t *)&t166) + 8));
                                            t169 = (uint8_t)gm_eqvs1pd373b1(((uint8_t *)&t170), ((uint8_t *)&t167));
                                        }
                                        uint8_t t171 = (uint8_t)(t169);
                                        gm_t5v8n6qrn4rh4 t172;
                                        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t172 + 8), t167);
                                        if (t171) {
                                            void * t173 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t174;
                                            GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sup5ecv5jiqbn));
                                            GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(118));
                                            gm_tftb1odc739t5 t175 = gm_f_extend_0cs85gs(t173, t174);
                                            gm_l43 = t175;
                                            uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                            if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srckg9scnip7e8g, 193, 21);
                                        } else {
                                            void * t177 = gm_l0;
                                            void * t178 = gm_l1;
                                            gm_t88rcucg1b7uk t179 = gm_l17;
                                            gm_b16_8 t180 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t177, t178, t179);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l24 = t180;
                                            void * t181 = gm_l0;
                                            void * t182 = gm_l1;
                                            gm_t88rcucg1b7uk t183 = gm_l17;
                                            gm_b16_8 t184 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t181, t182, t183);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l25 = t184;
                                            void * t185 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t186 = gm_l24;
                                            uint64_t t187 = gm_l22;
                                            uint64_t t188 = gm_l22;
                                            gm_b16_8 t189 = gm_l25;
                                            uint64_t t190 = gm_l22;
                                            uint64_t t191 = gm_l22;
                                            gm_tr4ummvbupakb t192;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t186);
                                            GM_ST(uint64_t, ((uint8_t *)&t192 + 16), t187);
                                            GM_ST(uint64_t, ((uint8_t *)&t192 + 24), t188);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t192 + 32), t189);
                                            GM_ST(uint64_t, ((uint8_t *)&t192 + 48), t190);
                                            GM_ST(uint64_t, ((uint8_t *)&t192 + 56), t191);
                                            gm_tftb1odc739t5 t193 = gm_f_write_aakaioo(t185, &t192);
                                            gm_l44 = t193;
                                            uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                            if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srckg9scnip7e8g, 197, 21);
                                            void * t195 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t196;
                                            GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sge6a56sqn6kj));
                                            GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(20));
                                            gm_tftb1odc739t5 t197 = gm_f_extend_0cs85gs(t195, t196);
                                            gm_l45 = t197;
                                            uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                            if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srckg9scnip7e8g, 198, 21);
                                        }
                                    }
                                    goto gm_lb11;
                                }
gm_lb18:;
                            }
                            {
                                {
                                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)17ULL));
                                    if (!t200) {
                                        goto gm_lb28;
                                    }
                                    void * t201 = gm_l1;
                                    gm_t88rcucg1b7uk t202 = gm_l2;
                                    void * t203 = gm_f_adt_of_gej99vn(t201, GM_CONV(gm_csguankko4lmhd, t202));
                                    {
                                        {
                                            uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t203) + 24) + 0));
                                            uint8_t t205 = (uint8_t)(t204 == ((uint8_t)1ULL));
                                            if (!t205) {
                                                goto gm_lb30;
                                            }
                                            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t203) + 24) + 8));
                                            gm_l26 = t206;
                                            void * t207 = gm_l1;
                                            gm_t88rcucg1b7uk t208 = gm_l26;
                                            gm_b16_8 t209 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t207, t208);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l27 = t209;
                                            void * t210 = gm_l1;
                                            gm_t88rcucg1b7uk t211 = gm_l2;
                                            gm_b16_8 t212 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t210, t211);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l46 = t212;
                                            void * t213 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t214)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t214, gm_srckg9scnip7e8g, 204, 26);
                                            uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)t213) + (uintptr_t)((uint64_t)0ULL) * 8));
                                            gm_l28 = t215;
                                            void * t216 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t217 = gm_l27;
                                            uint64_t t218 = gm_l28;
                                            gm_b16_8 t219 = gm_l27;
                                            uint64_t t220 = gm_l28;
                                            gm_thddbqb6snabm t221;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t221 + 0), t217);
                                            GM_ST(uint64_t, ((uint8_t *)&t221 + 16), t218);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t221 + 24), t219);
                                            GM_ST(uint64_t, ((uint8_t *)&t221 + 40), t220);
                                            gm_tftb1odc739t5 t222 = gm_f_write_91uvm3b(t216, &t221);
                                            gm_l47 = t222;
                                            uint8_t t223 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                            if (!t223) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srckg9scnip7e8g, 205, 21);
                                            goto gm_lb29;
                                        }
gm_lb30:;
                                        {
                                            uint8_t t224 = GM_LD(uint8_t, ((((uint8_t *)t203) + 24) + 0));
                                            uint8_t t225 = (uint8_t)(t224 == ((uint8_t)0ULL));
                                            if (!t225) {
                                                goto gm_lb31;
                                            }
                                            memmove(((uint8_t *)&F.l29), ((((uint8_t *)t203) + 24) + 8), 48);
                                            {
                                                {
                                                    uint8_t t226 = (uint8_t)(((((uint8_t *)&F.l29) + 40))[4] != 0);
                                                    if (!t226) {
                                                        goto gm_lb33;
                                                    }
                                                    gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 40));
                                                    gm_l30 = t227;
                                                    void * t228 = gm_l0;
                                                    void * t229 = gm_l1;
                                                    gm_t88rcucg1b7uk t230 = gm_l30;
                                                    gm_b16_8 t231 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t228, t229, t230);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l31 = t231;
                                                    void * t232 = (void *)((uint8_t *)&F.l5);
                                                    gm_b16_8 t233 = gm_l31;
                                                    gm_tnpifv20irnph t234;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t234 + 0), t233);
                                                    gm_tftb1odc739t5 t235 = gm_f_write_96p75mj(t232, t234);
                                                    gm_l48 = t235;
                                                    uint8_t t236 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                    if (!t236) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srckg9scnip7e8g, 210, 25);
                                                    goto gm_lb32;
                                                }
gm_lb33:;
                                                void * t237 = (void *)((uint8_t *)&F.l5);
                                                gm_b16_8 t238;
                                                GM_ST(void *, (uint8_t *)&t238, (void *)(gm_sug64ltbhov0m));
                                                GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(18));
                                                gm_tftb1odc739t5 t239 = gm_f_extend_0cs85gs(t237, t238);
                                                gm_l49 = t239;
                                                uint8_t t240 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                                if (!t240) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srckg9scnip7e8g, 212, 25);
                                            }
gm_lb32:;
                                            goto gm_lb29;
                                        }
gm_lb31:;
                                        {
                                            void * t241 = (void *)((uint8_t *)&F.l5);
                                            gm_b16_8 t242;
                                            GM_ST(void *, (uint8_t *)&t242, (void *)(gm_sug64ltbhov0m));
                                            GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(18));
                                            gm_tftb1odc739t5 t243 = gm_f_extend_0cs85gs(t241, t242);
                                            gm_l50 = t243;
                                            uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                            if (!t244) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srckg9scnip7e8g, 215, 22);
                                            goto gm_lb29;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 201, 27);
                                    }
gm_lb29:;
                                    goto gm_lb11;
                                }
gm_lb28:;
                            }
                            {
                                {
                                    void * t245 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t246;
                                    GM_ST(void *, (uint8_t *)&t246, (void *)(gm_sug64ltbhov0m));
                                    GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(18));
                                    gm_tftb1odc739t5 t247 = gm_f_extend_0cs85gs(t245, t246);
                                    gm_l51 = t247;
                                    uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                    if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srckg9scnip7e8g, 217, 18);
                                    goto gm_lb11;
                                }
gm_lb35:;
                                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 162, 14);
                            }
                        }
gm_lb11:;
                    }
                    {
                        goto gm_lb3;
                    }
                }
gm_lb10:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 157, 5);
            }
        }
gm_lb3:;
    }
    {
        void * t249 = gm_l0;
        gm_b16_8 t250 = gm_l4;
        gm_b16_8 t251;
        GM_ST(void *, (uint8_t *)&t251, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(4));
        gm_b16_8 t252;
        GM_ST(void *, (uint8_t *)&t252, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(34));
        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
        gm_b0_1 t255 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t249, t250, t251, t252, t254);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t256 = gm_l4;
        F.ret = t256;
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
inline __attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_helpers_4q783cn F;
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_uvfu1k1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_tl7apid((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_tl7apid((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_tl7apid((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t7;
    gm_f_new_namer_5q88o9g((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 40), t3);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 80), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 136), t5);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 192), t6);
    GM_ST(gm_ttiitul0263ld, ((uint8_t *)&(*(gm_te68172gglous *)gm_out) + 248), t7);
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

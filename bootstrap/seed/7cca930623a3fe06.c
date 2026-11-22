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
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_i2u6ls4 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_tbgr2dj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_overlays_6rdbiin {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_path_less_il538gm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l4; /* y */
    uint8_t ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_push_302k12h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5c6uc3s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g1u7f51(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qfqkllj(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint8_t gm_c021omeko26[8] __attribute__((aligned(1)));
extern uint8_t gm_c0nmfbvmuf3[21] __attribute__((aligned(1)));
extern uint8_t gm_c0uuouc2r1v[51] __attribute__((aligned(1)));
extern uint8_t gm_c12r28j4ti0[7] __attribute__((aligned(1)));
extern uint8_t gm_c1ud45er2tv[1] __attribute__((aligned(1)));
extern uint8_t gm_c291b4sguf9[11] __attribute__((aligned(1)));
extern uint8_t gm_c2dh2r1v1kg[35] __attribute__((aligned(1)));
extern uint8_t gm_c55hukqtjhj[3] __attribute__((aligned(1)));
extern uint8_t gm_c5efon5060b[11] __attribute__((aligned(1)));
extern uint8_t gm_c5hm1q1bhjb[3] __attribute__((aligned(1)));
extern uint8_t gm_c5mskvieaph[12] __attribute__((aligned(1)));
extern uint8_t gm_c69snm9duhi[16] __attribute__((aligned(8)));
extern uint8_t gm_c6l682gqq6b[11] __attribute__((aligned(1)));
extern uint8_t gm_c6rb5tij85l[11] __attribute__((aligned(1)));
extern uint8_t gm_c75ivhf79kk[12] __attribute__((aligned(1)));
extern uint8_t gm_c7de3eptq2i[7] __attribute__((aligned(1)));
extern uint8_t gm_c7oeondv7ro[15] __attribute__((aligned(1)));
extern uint8_t gm_c8fs1d0gfop[11] __attribute__((aligned(1)));
extern uint8_t gm_c8mhuortijc[14] __attribute__((aligned(1)));
extern uint8_t gm_ca5fsoiskc2[4160] __attribute__((aligned(8)));
extern uint8_t gm_capvqjk6u1v[2] __attribute__((aligned(1)));
extern uint8_t gm_cb66i48lh38[15] __attribute__((aligned(1)));
extern uint8_t gm_cbgiap4qvea[7] __attribute__((aligned(1)));
extern uint8_t gm_cbrgbb0abkn[87] __attribute__((aligned(1)));
extern uint8_t gm_cc5iio3s0t7[8] __attribute__((aligned(1)));
extern uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
extern uint8_t gm_cc8o9o6j033[1] __attribute__((aligned(1)));
extern uint8_t gm_celk2vfrrsg[5] __attribute__((aligned(1)));
extern uint8_t gm_celpgpm1fi8[1] __attribute__((aligned(1)));
extern uint8_t gm_cfmhli3rcvs[7] __attribute__((aligned(1)));
extern uint8_t gm_cg669l82cvi[9] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e_3[16] __attribute__((aligned(8)));
extern uint8_t gm_ch3qcmtapgq[1] __attribute__((aligned(1)));
extern uint8_t gm_chtf2n20g83[1] __attribute__((aligned(1)));
extern uint8_t gm_chv3533b78v[72] __attribute__((aligned(8)));
extern uint8_t gm_ckus03er382_3[4] __attribute__((aligned(4)));
extern uint8_t gm_cl1ove1i7cb[16] __attribute__((aligned(8)));
extern uint8_t gm_cm0gcp6hd1u[8] __attribute__((aligned(1)));
extern uint8_t gm_cmf6ku0sokq[14] __attribute__((aligned(1)));
extern uint8_t gm_cmj35tijhpe[7] __attribute__((aligned(1)));
extern uint8_t gm_cng4093cslh[14] __attribute__((aligned(1)));
extern uint8_t gm_cnjfl2vnhcn[10] __attribute__((aligned(1)));
extern uint8_t gm_cnst39lk6ll[77] __attribute__((aligned(1)));
extern uint8_t gm_coaen0t0mnr[13] __attribute__((aligned(1)));
extern uint8_t gm_cop8vf1t852[5] __attribute__((aligned(1)));
extern uint8_t gm_coqj6tssqes[9] __attribute__((aligned(1)));
extern uint8_t gm_cos4i51m3eh[4] __attribute__((aligned(1)));
extern uint8_t gm_cpkjrtfhmna[10] __attribute__((aligned(1)));
extern uint8_t gm_cpocp7udld7[1] __attribute__((aligned(1)));
extern uint8_t gm_cpr60jghihe[16] __attribute__((aligned(8)));
extern uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
extern uint8_t gm_cramav0v1j7[16] __attribute__((aligned(8)));
extern uint8_t gm_csshki16t34[4] __attribute__((aligned(4)));
extern uint8_t gm_cthdq3e6ori[7] __attribute__((aligned(1)));
extern uint8_t gm_ctq9itg1v7n[9] __attribute__((aligned(1)));
extern uint8_t gm_ctt52f5sga2[7] __attribute__((aligned(1)));
extern uint8_t gm_cuf2teap2n5[11] __attribute__((aligned(1)));
extern uint8_t gm_cumduqk6tqi[14] __attribute__((aligned(1)));
extern uint8_t gm_cuokidpd7vd[5] __attribute__((aligned(1)));
extern uint8_t gm_cv5npqnefda[6] __attribute__((aligned(1)));
extern uint8_t gm_cvf6snt0g1j[59] __attribute__((aligned(1)));
extern uint8_t gm_cvh4e3tm5ir[14] __attribute__((aligned(1)));
extern void *const gm_hl0b9itmh1di[];
extern void *const gm_hl5aqi4imlms[];
extern void *const gm_hlhb9f1a7omo[];
extern void *const gm_hlklsb3fd3g1[];
extern void *const gm_hlpd8uohstag[];
extern void *const gm_hlqu6cf845ie[];
extern void *const gm_hlrddq2io9s1[];
extern uint8_t gm_k1umcn50hea[40] __attribute__((aligned(8)));
extern uint8_t gm_k1umgjqrbkf[40] __attribute__((aligned(8)));
extern uint8_t gm_k5m9lemc4jn[40] __attribute__((aligned(8)));
extern uint8_t gm_k7td060pd86[40] __attribute__((aligned(8)));
extern uint8_t gm_k8majipqr8n[40] __attribute__((aligned(8)));
extern uint8_t gm_k8nmk7je8im[40] __attribute__((aligned(8)));
extern uint8_t gm_ka38s9qhgtd[40] __attribute__((aligned(8)));
extern uint8_t gm_ka5jrrkpb0m[40] __attribute__((aligned(8)));
extern uint8_t gm_kakc8s3pa9o[40] __attribute__((aligned(8)));
extern uint8_t gm_kbhckstt7he[40] __attribute__((aligned(8)));
extern uint8_t gm_kbnbrskseut[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1avcjqd98[40] __attribute__((aligned(8)));
extern uint8_t gm_kgk1u1jppkr[40] __attribute__((aligned(8)));
extern uint8_t gm_kgubkej3sov[40] __attribute__((aligned(8)));
extern uint8_t gm_khov2p23dnf[40] __attribute__((aligned(8)));
extern uint8_t gm_khqbs9bi0vi[40] __attribute__((aligned(8)));
extern uint8_t gm_khsmm7kmq9o[40] __attribute__((aligned(8)));
extern uint8_t gm_kil3h1h1ecr[40] __attribute__((aligned(8)));
extern uint8_t gm_kj662j6koce[40] __attribute__((aligned(8)));
extern uint8_t gm_kn81j57eee2[40] __attribute__((aligned(8)));
extern uint8_t gm_kolm4drqk8i[40] __attribute__((aligned(8)));
extern uint8_t gm_kp8bqs68o7b[40] __attribute__((aligned(8)));
extern uint8_t gm_ktlk7p7kh6n[40] __attribute__((aligned(8)));
extern uint8_t gm_kuspe9g7ejq[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01aerdf23b33[14];
extern const uint8_t gm_s02aodrl2fpra[12];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s049rlfhk3i2d[6];
extern const uint8_t gm_s04a35h53le03[43];
extern const uint8_t gm_s08dcqdp58r5f[3];
extern const uint8_t gm_s08ekjvgo9fro[101];
extern const uint8_t gm_s091lteo20dar[64];
extern const uint8_t gm_s0a0jsggm04iu[8];
extern const uint8_t gm_s0ad3307s8jmo[105];
extern const uint8_t gm_s0adesbq24e9u[6];
extern const uint8_t gm_s0bpji6inee0r[19];
extern const uint8_t gm_s0e9nt7u0mvf9[66];
extern const uint8_t gm_s0i56e621jvcv[41];
extern const uint8_t gm_s0ibfi39mcc7n[9];
extern const uint8_t gm_s0j5c49gnifu8[18];
extern const uint8_t gm_s0l72j5pkqi6s[9];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0mpcnom7nq2p[47];
extern const uint8_t gm_s0mph2pjoa3np[17];
extern const uint8_t gm_s0n03nrf296lf[33];
extern const uint8_t gm_s0nnk79up541r[32];
extern const uint8_t gm_s0ph7fnc0ftge[16];
extern const uint8_t gm_s0q6kpmhsprri[33];
extern const uint8_t gm_s0qbjm15aot6s[11];
extern const uint8_t gm_s0rfpnv4c3k1l[38];
extern const uint8_t gm_s0t5omnh472sf[21];
extern const uint8_t gm_s0ug1ukauk3l9[74];
extern const uint8_t gm_s0ve0dba4rtj8[13];
extern const uint8_t gm_s11aakm4d1c2s[19];
extern const uint8_t gm_s14lpmr3v2vk3[57];
extern const uint8_t gm_s15i3qlq36ep1[16];
extern const uint8_t gm_s16bsnuu7i04s[3];
extern const uint8_t gm_s1a1r1qe3ssot[9];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1d673tn03i3r[5];
extern const uint8_t gm_s1ekjot5tdpje[67];
extern const uint8_t gm_s1gf327eapo1j[11];
extern const uint8_t gm_s1kvbdtols1cc[53];
extern const uint8_t gm_s1l7s35k2bj17[8];
extern const uint8_t gm_s1p5ld7i8mkfu[10];
extern const uint8_t gm_s1q0n6t6d7g6n[34];
extern const uint8_t gm_s1qfo73r5cemc[64];
extern const uint8_t gm_s1r46q0dlpfeo[41];
extern const uint8_t gm_s1tqe3mtj6vb1[8];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s21bv1o9t2tge[11];
extern const uint8_t gm_s2254b9hkee3b[6];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s25eqlrovn9m2[41];
extern const uint8_t gm_s26o5frgr2hds[7];
extern const uint8_t gm_s27pt4vcvtscl[24];
extern const uint8_t gm_s2bjh3dbncrc8[3];
extern const uint8_t gm_s2e5tbp7977b1[11];
extern const uint8_t gm_s2g9gpcumahrc[24];
extern const uint8_t gm_s2gjlt9eraf0q[10];
extern const uint8_t gm_s2hbi2g88pale[19];
extern const uint8_t gm_s2j1m46v0uc4p[10];
extern const uint8_t gm_s2jl146j9vpnj[20];
extern const uint8_t gm_s2kl2d59c04nf[19];
extern const uint8_t gm_s2kmbpuutp9qq[5];
extern const uint8_t gm_s2odv5tht4lj8[20];
extern const uint8_t gm_s2u5mf7vjj6ie[24];
extern const uint8_t gm_s2vme195ddtm7[25];
extern const uint8_t gm_s2vmmrs8i9iqs[4];
extern const uint8_t gm_s31c4u1mtb865[41];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s33fm62gcdbhu[11];
extern const uint8_t gm_s33ft5ck7snhl[4];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s34n6pusofh71[8];
extern const uint8_t gm_s37v6u2dqp80f[5];
extern const uint8_t gm_s38dtnjej3849[41];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3bb7jrtcd5ot[15];
extern const uint8_t gm_s3brf0f9sa1j7[26];
extern const uint8_t gm_s3dk9gdukms7b[18];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3hhdo4dfru76[7];
extern const uint8_t gm_s3hinmnodmkmv[21];
extern const uint8_t gm_s3iij9a939gcv[15];
extern const uint8_t gm_s3ilkivmnrfk4[28];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3k9reqbrnuns[25];
extern const uint8_t gm_s3kfpoo7v2f2o[27];
extern const uint8_t gm_s3lbmpmt95hvh[7];
extern const uint8_t gm_s3m769if3iik9[32];
extern const uint8_t gm_s3mnje3d1c6qa[27];
extern const uint8_t gm_s3mqrb8c7qaar[9];
extern const uint8_t gm_s3n3t2qjk9dg2[5];
extern const uint8_t gm_s3ndmfc1bk22f[23];
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s3o37fp7c0sqt[15];
extern const uint8_t gm_s3op5s1agh0ju[35];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3r7cg0hbrkdg[17];
extern const uint8_t gm_s3s76fqo1a9jk[24];
extern const uint8_t gm_s45fljr5t8358[4];
extern const uint8_t gm_s47ic12e9ejh1[41];
extern const uint8_t gm_s4a6h3o2n8s33[94];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4c0t3s0kj31v[12];
extern const uint8_t gm_s4fi69869ooj8[28];
extern const uint8_t gm_s4g3rq58567m6[8];
extern const uint8_t gm_s4gc37vr9terk[58];
extern const uint8_t gm_s4h2qpm1qm9a0[8];
extern const uint8_t gm_s4hp8mhves75a[41];
extern const uint8_t gm_s4i05mf7gcfb7[17];
extern const uint8_t gm_s4jglh3q4fib8[12];
extern const uint8_t gm_s4jhjvgae4896[13];
extern const uint8_t gm_s4k55k154s62m[120];
extern const uint8_t gm_s4k57chtk2mkc[88];
extern const uint8_t gm_s4l2s2smm9apo[21];
extern const uint8_t gm_s4m9nol66dh0a[25];
extern const uint8_t gm_s4n5m9kt2s9ld[27];
extern const uint8_t gm_s4nddosr4taav[5];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s4qg6lddtahb8[4];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s4rugtmn8jarn[57];
extern const uint8_t gm_s4sak9mcv04d9[25];
extern const uint8_t gm_s4v8j7s5sr5c1[66];
extern const uint8_t gm_s4vgbnvmvj0st[34];
extern const uint8_t gm_s4vqnsfknimfg[28];
extern const uint8_t gm_s50uau4el7or4[17];
extern const uint8_t gm_s511vp5vjnnnk[68];
extern const uint8_t gm_s523ivho5m3dq[13];
extern const uint8_t gm_s52od60hi0cpi[21];
extern const uint8_t gm_s53c18pcajigs[51];
extern const uint8_t gm_s541nrvershlm[7];
extern const uint8_t gm_s5c0ruf0vg8mu[3];
extern const uint8_t gm_s5ed1f66u9mdp[28];
extern const uint8_t gm_s5el4r6boe881[14];
extern const uint8_t gm_s5fcnrkaual42[41];
extern const uint8_t gm_s5fk67ihob1ic[41];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5ocg5vi2qib3[21];
extern const uint8_t gm_s5ojatldfpfs2[8];
extern const uint8_t gm_s5p7bqbmkba97[13];
extern const uint8_t gm_s5tgvdli5u3js[17];
extern const uint8_t gm_s5v003pfcdo1r[22];
extern const uint8_t gm_s6077qnlc299h[4];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s684ivkkpni41[7];
extern const uint8_t gm_s68voc4rs92m2[15];
extern const uint8_t gm_s6ac0uac1dtsl[62];
extern const uint8_t gm_s6b2qpq3tgsj8[12];
extern const uint8_t gm_s6c128h826nsg[25];
extern const uint8_t gm_s6cs6skh2j334[17];
extern const uint8_t gm_s6f3urtqqu41d[16];
extern const uint8_t gm_s6i1ull1350kv[5];
extern const uint8_t gm_s6igeulorpagn[14];
extern const uint8_t gm_s6ket4hq2umi0[51];
extern const uint8_t gm_s6nm27ehg597q[7];
extern const uint8_t gm_s6qvm08vrap69[4];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6tv3tkgkodqm[12];
extern const uint8_t gm_s6v08aos37nfe[8];
extern const uint8_t gm_s70bck1t85qpv[9];
extern const uint8_t gm_s71j6n6s8up14[17];
extern const uint8_t gm_s72l6okdj2d3g[95];
extern const uint8_t gm_s72tasmt4pg49[26];
extern const uint8_t gm_s7345rpped3bc[41];
extern const uint8_t gm_s736j3npg82a4[6];
extern const uint8_t gm_s73vhpsrl3o41[31];
extern const uint8_t gm_s756ctefg4er5[59];
extern const uint8_t gm_s75snq6p722c8[19];
extern const uint8_t gm_s77fi68iu5q6n[5];
extern const uint8_t gm_s7a0708p0cdgv[38];
extern const uint8_t gm_s7agona8glbka[13];
extern const uint8_t gm_s7bqfholfgtfq[13];
extern const uint8_t gm_s7ddabri39fvk[9];
extern const uint8_t gm_s7g0chs5u30do[21];
extern const uint8_t gm_s7h5sk98p4bgn[17];
extern const uint8_t gm_s7hi4f3a8v74u[13];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7ifmhl5viqvs[10];
extern const uint8_t gm_s7jhnrnbmkbbq[39];
extern const uint8_t gm_s7l31qd1tkp00[41];
extern const uint8_t gm_s7mie2m30h1s9[28];
extern const uint8_t gm_s7s25m3nlrif2[6];
extern const uint8_t gm_s7teaveu6apab[4];
extern const uint8_t gm_s809b2tff170u[13];
extern const uint8_t gm_s823123ng02vo[44];
extern const uint8_t gm_s82anase9fhnd[8];
extern const uint8_t gm_s82d2namrcvs3[17];
extern const uint8_t gm_s8575j3cip1vh[36];
extern const uint8_t gm_s86uh6df2eunn[20];
extern const uint8_t gm_s88lqrfvhc8i3[46];
extern const uint8_t gm_s88rffcs9cj2p[12];
extern const uint8_t gm_s898uefj5c9k6[28];
extern const uint8_t gm_s89md2uvrtjfh[14];
extern const uint8_t gm_s8abg3vemofr6[4];
extern const uint8_t gm_s8b01h2tetj3e[45];
extern const uint8_t gm_s8cup0kpdd4dj[14];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s8guqcjcce9oc[22];
extern const uint8_t gm_s8h39pil8lgh0[41];
extern const uint8_t gm_s8ivbu50c9tsq[9];
extern const uint8_t gm_s8mnqic20r702[7];
extern const uint8_t gm_s8mre3qset1hp[60];
extern const uint8_t gm_s8pvtaofn65p9[29];
extern const uint8_t gm_s8ta4feq24fk9[12];
extern const uint8_t gm_s9236sqnmc8uc[9];
extern const uint8_t gm_s967qbtu9ltck[12];
extern const uint8_t gm_s97nfli622uqg[5];
extern const uint8_t gm_s989kd388iles[5];
extern const uint8_t gm_s98se7jporda2[72];
extern const uint8_t gm_s9b81q4kouna6[11];
extern const uint8_t gm_s9beklm4tc5k5[193];
extern const uint8_t gm_s9bggu8oepdvs[19];
extern const uint8_t gm_s9bnhibq0fug8[5];
extern const uint8_t gm_s9e7jg1nv469k[6];
extern const uint8_t gm_s9ff7kbg5s1vv[10];
extern const uint8_t gm_s9iu5gsqog7uc[5];
extern const uint8_t gm_s9krd4pni7gv1[6];
extern const uint8_t gm_s9lclteeapkg2[3];
extern const uint8_t gm_s9md3pg6vlt5h[3];
extern const uint8_t gm_s9mhd8h7bolj3[48];
extern const uint8_t gm_s9nk06pmq8hl1[34];
extern const uint8_t gm_s9ok67plsnbj1[7];
extern const uint8_t gm_s9pdhp63uj9bd[7];
extern const uint8_t gm_s9r71kmnlqpv8[17];
extern const uint8_t gm_s9rhhj2j5hs2v[36];
extern const uint8_t gm_sa0ep7oknkrm1[18];
extern const uint8_t gm_sa15idp716r2q[12];
extern const uint8_t gm_sa36vve0410hq[7];
extern const uint8_t gm_sa3obh41s8f7p[70];
extern const uint8_t gm_sa42h1oh66nds[9];
extern const uint8_t gm_sa6br7uea4stj[69];
extern const uint8_t gm_sa70lf0p32gn5[28];
extern const uint8_t gm_sa71447djt33j[49];
extern const uint8_t gm_sa9o54tgja188[12];
extern const uint8_t gm_sa9ukgikn2pki[26];
extern const uint8_t gm_sah1jmr01nidj[5];
extern const uint8_t gm_sahahh1a40c5c[37];
extern const uint8_t gm_sahjmrj1mjf77[8];
extern const uint8_t gm_sajbccb18c45d[6];
extern const uint8_t gm_sajo3kdi51jpa[10];
extern const uint8_t gm_sam1nkbp8dhs9[16];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saq7q763oq882[48];
extern const uint8_t gm_sar5aak6f8jpo[44];
extern const uint8_t gm_sarlhchtvv77m[13];
extern const uint8_t gm_sasg9nroe4gk1[4];
extern const uint8_t gm_sasocllalvlsg[7];
extern const uint8_t gm_saturamfhig5d[45];
extern const uint8_t gm_saveva62qedei[8];
extern const uint8_t gm_sb02cov56i977[8];
extern const uint8_t gm_sb281auin9fds[20];
extern const uint8_t gm_sb2rimopi40u7[39];
extern const uint8_t gm_sb5b70djeqhg8[16];
extern const uint8_t gm_sb5j6apm5mfoq[31];
extern const uint8_t gm_sb658vbovt6jg[37];
extern const uint8_t gm_sb71rivinn7pn[10];
extern const uint8_t gm_sb7kik80lfbge[11];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbabluaguu2bc[51];
extern const uint8_t gm_sbe2lidu87fed[61];
extern const uint8_t gm_sbg66h7153j4l[16];
extern const uint8_t gm_sbgcf6rjaqh5h[8];
extern const uint8_t gm_sbgepjj6hgjej[4];
extern const uint8_t gm_sbo1c1kpq53tj[31];
extern const uint8_t gm_sbogo0q916fbh[19];
extern const uint8_t gm_sbuhie0vb9t62[9];
extern const uint8_t gm_sbupitjaj376b[34];
extern const uint8_t gm_sbvdm2nk31pvh[9];
extern const uint8_t gm_sc12loraoaq3m[7];
extern const uint8_t gm_sc17hqmokdbc2[41];
extern const uint8_t gm_sc24a25pi012e[8];
extern const uint8_t gm_sc3pq8972q5i0[15];
extern const uint8_t gm_sc5qopgd7dm66[8];
extern const uint8_t gm_scacg345bm166[69];
extern const uint8_t gm_scc5n7285iivu[13];
extern const uint8_t gm_sce9ak22luehr[27];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_scfama5f5jm1j[25];
extern const uint8_t gm_scfp9v0kro0hh[17];
extern const uint8_t gm_scggaq5euuhe7[19];
extern const uint8_t gm_sciqh24mne54a[4];
extern const uint8_t gm_sckqm6be9dvme[13];
extern const uint8_t gm_scl6is0vlr1m0[62];
extern const uint8_t gm_scmcg8350emvu[19];
extern const uint8_t gm_scrq475mutg06[4];
extern const uint8_t gm_scttf7jtr4fn9[6];
extern const uint8_t gm_scumr3cdeol3i[13];
extern const uint8_t gm_scvd4ia8nkih4[17];
extern const uint8_t gm_scviua4g9o7rp[62];
extern const uint8_t gm_sd0rfmo1vm5au[12];
extern const uint8_t gm_sd2ioiubqiqs5[46];
extern const uint8_t gm_sd5r0412dj6fb[15];
extern const uint8_t gm_sd6t9e70pfcub[29];
extern const uint8_t gm_sd9regb2pbgv1[8];
extern const uint8_t gm_sda1bf43v6985[10];
extern const uint8_t gm_sdajtpr06648l[6];
extern const uint8_t gm_sdbekdpro29kh[39];
extern const uint8_t gm_sdbvbbg6vc50j[27];
extern const uint8_t gm_sdd9j9qgq2abj[29];
extern const uint8_t gm_sddmbtkcc2n83[11];
extern const uint8_t gm_sdeu8nuc597e8[19];
extern const uint8_t gm_sdft8ov6bb7s6[8];
extern const uint8_t gm_sdhlphf1ucpbo[18];
extern const uint8_t gm_sdj9af8hu07uk[18];
extern const uint8_t gm_sdjfsrjr2b2ke[2];
extern const uint8_t gm_sdqn7n16togpr[29];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sdt2alvv2h6jh[48];
extern const uint8_t gm_sdu3h28udk8s9[13];
extern const uint8_t gm_sdu7bd5v9q750[65];
extern const uint8_t gm_sduk7s8m3amo6[12];
extern const uint8_t gm_se1jf3monjum8[10];
extern const uint8_t gm_se23d1hoq85r1[5];
extern const uint8_t gm_se3cg8f7o7u1d[12];
extern const uint8_t gm_se7uf9ek5fjqq[18];
extern const uint8_t gm_sear9h0hhleaj[6];
extern const uint8_t gm_secneq49c8dtf[36];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_seh12nehvgmhj[12];
extern const uint8_t gm_sejtof4qcrkoq[8];
extern const uint8_t gm_selmtbgalekup[10];
extern const uint8_t gm_semec5mr3jo2t[73];
extern const uint8_t gm_semhbv8seph85[21];
extern const uint8_t gm_semskfu4esg5u[17];
extern const uint8_t gm_sepu95c7o773o[6];
extern const uint8_t gm_seqv335lv6284[41];
extern const uint8_t gm_serk0pt9jcvpr[14];
extern const uint8_t gm_sesqjijoh7km4[9];
extern const uint8_t gm_setdfq42nhg1q[5];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sf20puqngp9vm[9];
extern const uint8_t gm_sf22i79i4hbq1[7];
extern const uint8_t gm_sf37jkpot59cu[54];
extern const uint8_t gm_sf4ahu9mfer7v[5];
extern const uint8_t gm_sf4qubhm4h2hm[32];
extern const uint8_t gm_sf5jsgjp29cqn[14];
extern const uint8_t gm_sf9nvp09l65v3[17];
extern const uint8_t gm_sfcgvtliokb7a[28];
extern const uint8_t gm_sfd2hpva2vj33[20];
extern const uint8_t gm_sfeb57kq1r2n4[4];
extern const uint8_t gm_sfflovsm8rlpo[51];
extern const uint8_t gm_sfg8mkf2826qa[5];
extern const uint8_t gm_sfggcm2ak7679[20];
extern const uint8_t gm_sfhb5i19r421v[6];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfled84a022e2[50];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfrcaft0lo33k[60];
extern const uint8_t gm_sfrrtib86oiig[23];
extern const uint8_t gm_sfve13r15sgkv[41];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg25rp8qmrbme[57];
extern const uint8_t gm_sg26if17vtome[23];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg58j44mq0qb5[61];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg90up8jjg51i[15];
extern const uint8_t gm_sga7ge5dc8qdr[50];
extern const uint8_t gm_sgal4nbdtbtvo[18];
extern const uint8_t gm_sgbcvt6rqctvd[7];
extern const uint8_t gm_sgbib360sn2mu[131];
extern const uint8_t gm_sgbklk5omjecv[29];
extern const uint8_t gm_sgchj7ubg6umm[20];
extern const uint8_t gm_sgemh066sqogf[4];
extern const uint8_t gm_sgg5s9bhkgqj3[41];
extern const uint8_t gm_sgip9ekbho6mc[11];
extern const uint8_t gm_sgks95jf6fs73[21];
extern const uint8_t gm_sgkuqlapm8vur[12];
extern const uint8_t gm_sgnt3t4lgofos[35];
extern const uint8_t gm_sgooul7ab8sa1[65];
extern const uint8_t gm_sgpahsg79gte4[26];
extern const uint8_t gm_sgrkf3bk77nul[17];
extern const uint8_t gm_sgrv5qufpkvf8[7];
extern const uint8_t gm_sgtlutvecf7kf[46];
extern const uint8_t gm_sh1e27cf0k9if[6];
extern const uint8_t gm_sh4b8t54h2jna[49];
extern const uint8_t gm_sh5prd89hpoup[85];
extern const uint8_t gm_sh7dg9dli11hk[50];
extern const uint8_t gm_sh7ob55fi0i62[26];
extern const uint8_t gm_sha819aic5lkk[22];
extern const uint8_t gm_shakeh6dfb84i[17];
extern const uint8_t gm_shde4bsckk9mt[4];
extern const uint8_t gm_shdh4dd2ejebj[158];
extern const uint8_t gm_sheb91gbrs2c3[41];
extern const uint8_t gm_shesnhdok5fgk[55];
extern const uint8_t gm_shg24ogv0rnt4[14];
extern const uint8_t gm_shh36vc8p1t4e[7];
extern const uint8_t gm_shjri07u8kc84[27];
extern const uint8_t gm_shl5hekt9ba4l[19];
extern const uint8_t gm_shmknkannurll[5];
extern const uint8_t gm_shmpls8n0g8h9[4];
extern const uint8_t gm_shn9bhhpahcjo[16];
extern const uint8_t gm_shne2m0cj5kgi[6];
extern const uint8_t gm_shnk1stovqggp[5];
extern const uint8_t gm_shqein5ip3gpn[19];
extern const uint8_t gm_shqvr1vn78f0b[54];
extern const uint8_t gm_shrelr880pj0m[7];
extern const uint8_t gm_shsdmcj9g7u41[6];
extern const uint8_t gm_shsrp9mgu2tm3[41];
extern const uint8_t gm_sht5iivcbi64u[12];
extern const uint8_t gm_shtuc6se4lilq[7];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_shv6kkd8qg13e[38];
extern const uint8_t gm_si2pj4mpn1kn5[11];
extern const uint8_t gm_si3s44ll3u1p2[42];
extern const uint8_t gm_si4l87dl4tpkq[8];
extern const uint8_t gm_si56o26p8hqsl[8];
extern const uint8_t gm_si5gf6931vagv[11];
extern const uint8_t gm_si68bogf7eu32[8];
extern const uint8_t gm_si6fvebhspqjd[15];
extern const uint8_t gm_si6ooauhec559[5];
extern const uint8_t gm_si6sr5gqkcui7[6];
extern const uint8_t gm_si7vj1dcaqhfd[5];
extern const uint8_t gm_sica240oidke0[8];
extern const uint8_t gm_sidlbibo929o4[12];
extern const uint8_t gm_sidmu4drv3rks[19];
extern const uint8_t gm_sie82oc0ua5dm[3];
extern const uint8_t gm_sif4kr35r5dhh[81];
extern const uint8_t gm_siflgunfq3r7b[11];
extern const uint8_t gm_sihb3eljrjar6[16];
extern const uint8_t gm_sik1l49uru5dh[13];
extern const uint8_t gm_sims8gu714a9a[14];
extern const uint8_t gm_sio6bogdhis0p[2];
extern const uint8_t gm_siqgrmkn8n4vn[8];
extern const uint8_t gm_sisp52otedii5[23];
extern const uint8_t gm_siuqnp32vjkps[8];
extern const uint8_t gm_sivdpq4bdf3h5[17];
extern const uint8_t gm_sj0tu4mea4egk[5];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sj2f7h0km68ut[32];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_sj3cmvbmi7cm6[9];
extern const uint8_t gm_sj7osq6sb8i2r[108];
extern const uint8_t gm_sjb713eltfa88[112];
extern const uint8_t gm_sjbjjlvp0cfb7[12];
extern const uint8_t gm_sjc00l2ljprs2[4];
extern const uint8_t gm_sjcgs2v314rl7[19];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjcosulco987j[98];
extern const uint8_t gm_sjf1r2hmheh87[29];
extern const uint8_t gm_sjfdnh735o7f8[5];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjifgfpiu6k52[6];
extern const uint8_t gm_sjl0s4joed61f[15];
extern const uint8_t gm_sjljm9omp36ob[52];
extern const uint8_t gm_sjltai3b82h79[4];
extern const uint8_t gm_sjpstm6i2bq0k[417];
extern const uint8_t gm_sjvgkv27ovi7g[48];
extern const uint8_t gm_sk2tgsi47prt7[14];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_skapbb3maknat[5];
extern const uint8_t gm_skd5c72k4pcr6[23];
extern const uint8_t gm_skdv3lmatcupg[21];
extern const uint8_t gm_skf282h8e3q1e[49];
extern const uint8_t gm_skfdav0cuo9ri[14];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skfrgfcfg8vrf[36];
extern const uint8_t gm_skgulapvvrhkp[36];
extern const uint8_t gm_skh04d6p3n402[79];
extern const uint8_t gm_skh4rkm9k1elb[8];
extern const uint8_t gm_skjltb6l69n5p[11];
extern const uint8_t gm_sktjfeu2a0ava[15];
extern const uint8_t gm_sl0n65k3lfps6[18];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_sl34v6fvasecs[31];
extern const uint8_t gm_sl41530g3ehjt[8];
extern const uint8_t gm_sld9l7nonn4o9[17];
extern const uint8_t gm_slff7lshtgu6r[42];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slgnoj3l4ks2a[17];
extern const uint8_t gm_slhllpfip1vpg[3];
extern const uint8_t gm_slipvb5272ocn[4];
extern const uint8_t gm_slj1im0lsqj9h[32];
extern const uint8_t gm_slk0i3scss4m6[11];
extern const uint8_t gm_sll03ulrg0frn[81];
extern const uint8_t gm_slpekm89q5lb3[29];
extern const uint8_t gm_slrfdinnoh7ti[41];
extern const uint8_t gm_sltqebpmj61tp[3];
extern const uint8_t gm_sluco9qbar70k[10];
extern const uint8_t gm_slvu7caka1qb7[10];
extern const uint8_t gm_sm03uudimlmgi[59];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm7p819bg8637[13];
extern const uint8_t gm_sm9m81v5v04r9[12];
extern const uint8_t gm_smab0ikt4jd4g[17];
extern const uint8_t gm_smaguor10so8u[12];
extern const uint8_t gm_smarqt2mi26ea[7];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smd171q6j6al8[20];
extern const uint8_t gm_smdjnv4d2cfrh[31];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smgfld86ipv3f[58];
extern const uint8_t gm_smhebipq24jdo[28];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smk08v391jpq1[23];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smos9ggka3uc3[10];
extern const uint8_t gm_smpdttgra9a1b[85];
extern const uint8_t gm_smurk4n2ra6mi[11];
extern const uint8_t gm_smv96er1uehm8[94];
extern const uint8_t gm_smviu3logd6du[4];
extern const uint8_t gm_sn0785fueq6ki[64];
extern const uint8_t gm_sn12gub8tjj5h[6];
extern const uint8_t gm_sn3tcnrle7f4r[4];
extern const uint8_t gm_sn4gb2qts08ck[14];
extern const uint8_t gm_sn5m7a373k04g[45];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_sn8v0ub9fm41i[41];
extern const uint8_t gm_snaenr23tbgmb[23];
extern const uint8_t gm_snboa22b96bbc[4];
extern const uint8_t gm_snc5ns3akncuq[6];
extern const uint8_t gm_snf877edekrff[11];
extern const uint8_t gm_snftglmif3ta2[11];
extern const uint8_t gm_sngckqdbegtlc[26];
extern const uint8_t gm_snhkhrbd8tjtf[45];
extern const uint8_t gm_snk5sdi9p8rv7[5];
extern const uint8_t gm_snl3g51f7tu9o[12];
extern const uint8_t gm_snoq0o06l5uns[15];
extern const uint8_t gm_snps96dj7ecq0[17];
extern const uint8_t gm_snqbu1qqmr5lp[4];
extern const uint8_t gm_snr2l2v2pqfua[4];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snseiioj5sbgs[2];
extern const uint8_t gm_sntddact5d6vp[4];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snvn4jaklkik3[65];
extern const uint8_t gm_snvncjka7ph6m[7];
extern const uint8_t gm_snvodad5t43r7[11];
extern const uint8_t gm_so05t2goj6895[24];
extern const uint8_t gm_so26uaund1443[41];
extern const uint8_t gm_so2pnermu207h[23];
extern const uint8_t gm_so3hnoui8sg80[12];
extern const uint8_t gm_so3qrsvnd4euf[12];
extern const uint8_t gm_so3ukp67pds22[190];
extern const uint8_t gm_so4mj8r7k5aek[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so52klh2mgoce[23];
extern const uint8_t gm_so597te7oecin[72];
extern const uint8_t gm_so81pe3qfh0hc[15];
extern const uint8_t gm_sobnp9i68hvdj[16];
extern const uint8_t gm_sodgjk6pvm3b4[21];
extern const uint8_t gm_sodi7jjra685n[6];
extern const uint8_t gm_soeesub7ccc96[31];
extern const uint8_t gm_sogv4el7uffui[58];
extern const uint8_t gm_soi4hbchhjloe[4];
extern const uint8_t gm_soka1k58t5da3[16];
extern const uint8_t gm_sokpg5qd8r9va[20];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sono5n5lrjd7h[10];
extern const uint8_t gm_soo0b43e4t7sn[12];
extern const uint8_t gm_sop5f3b03uipi[9];
extern const uint8_t gm_soqhrfmr1feii[7];
extern const uint8_t gm_sor8ce07jk3r2[10];
extern const uint8_t gm_sovn1qud9dmh0[70];
extern const uint8_t gm_sp0ag7curvl4o[42];
extern const uint8_t gm_sp3vt8erg7nae[41];
extern const uint8_t gm_sp4426g665gvh[53];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_sp7fbepv94vjk[3];
extern const uint8_t gm_sp8ppesjprts0[67];
extern const uint8_t gm_spa4t2l419v39[14];
extern const uint8_t gm_spf5krhuq011v[11];
extern const uint8_t gm_spfda095eoqse[34];
extern const uint8_t gm_spg4d1df6jnji[7];
extern const uint8_t gm_spj9rva8kb5ar[3];
extern const uint8_t gm_spki3o9t7n682[4];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_spm0cds2upaql[7];
extern const uint8_t gm_spmv5l6cklh5d[18];
extern const uint8_t gm_spocqdbj2524b[20];
extern const uint8_t gm_sppeomacjvkpm[7];
extern const uint8_t gm_spqmq1ln67me5[79];
extern const uint8_t gm_sps7gfr3iknaa[5];
extern const uint8_t gm_spvfph5mfb023[16];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const uint8_t gm_sq12h87ngj5nl[9];
extern const uint8_t gm_sq64eqmcuoej6[15];
extern const uint8_t gm_sq6kdq5587s3n[11];
extern const uint8_t gm_sq75i0jov46r1[63];
extern const uint8_t gm_sq98g0itpb0cn[19];
extern const uint8_t gm_sq9jk07jm2tgi[10];
extern const uint8_t gm_sq9ngpqr0m7ek[31];
extern const uint8_t gm_sqb8noo4kgsmi[12];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqjlosotosjfp[18];
extern const uint8_t gm_sqk8phuvunvgk[56];
extern const uint8_t gm_sqkn3op33hp4i[24];
extern const uint8_t gm_sql0in48r61nd[5];
extern const uint8_t gm_sql5f30410gqn[20];
extern const uint8_t gm_sqlp5qu42o606[11];
extern const uint8_t gm_sqmd0r48ameas[7];
extern const uint8_t gm_sqqlqus41v6vq[12];
extern const uint8_t gm_sqr1rjjq3dpuh[17];
extern const uint8_t gm_sqr7arfv61pma[9];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sqri8tum3tpfm[3];
extern const uint8_t gm_sqs6r7f5gl2vu[26];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_squv1ktk9abst[41];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr26j6lp0gfqo[11];
extern const uint8_t gm_sr2oeunjjbqtf[14];
extern const uint8_t gm_sr2qa646easkn[56];
extern const uint8_t gm_sr3et5tffndla[28];
extern const uint8_t gm_sr3suba4tbk8h[33];
extern const uint8_t gm_sr486u97qk9sk[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr54681eccerj[11];
extern const uint8_t gm_sr5hmn4fnbh3s[3];
extern const uint8_t gm_sr82af5e87dso[54];
extern const uint8_t gm_sr8m5cvuum3fb[5];
extern const uint8_t gm_sr8o3r2mb0he1[81];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3ukoldgd7ran[];
extern const char gm_src563df9lf2t92[];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_src8lmgjh8bsvgp[];
extern const char gm_src8pvk8vd1qnr1[];
extern const char gm_src947s096ojljr[];
extern const char gm_srceoep1gquqdsq[];
extern const char gm_srcfjrpo4i6b0at[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srclu7allsekqhf[];
extern const char gm_srcml1jlm4abpm3[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcok2brbaepru3[];
extern const char gm_srcp2gphcvgkhg0[];
extern const char gm_srcp33bek143l58[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuro7l7pd15c2[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_sri0h4dm5d3ul[17];
extern const uint8_t gm_srjo6vku5gka5[20];
extern const uint8_t gm_srlklqc70q6ju[3];
extern const uint8_t gm_srltb43nuabaq[5];
extern const uint8_t gm_srq5utruoa2v3[28];
extern const uint8_t gm_srrjdhjujsp18[16];
extern const uint8_t gm_srrqh3pev0igg[9];
extern const uint8_t gm_srsbjtfrhs8q1[16];
extern const uint8_t gm_srsr8fvlin2g6[65];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_srvi01p0as8h9[15];
extern const uint8_t gm_srvl2rg1hpk9a[16];
extern const uint8_t gm_ss0g40l133d39[13];
extern const uint8_t gm_ss34ilcmun9p0[19];
extern const uint8_t gm_ss3f6irsroj9l[37];
extern const uint8_t gm_ss3m7p8o4vdj0[41];
extern const uint8_t gm_ss3tptge0i2av[36];
extern const uint8_t gm_ss5q3rrcb010f[27];
extern const uint8_t gm_ss8lvrvkhracl[21];
extern const uint8_t gm_ss8mnjves3ogt[12];
extern const uint8_t gm_ss9fj8tanu5jn[51];
extern const uint8_t gm_ssbcl1ouchrlk[39];
extern const uint8_t gm_ssbqeok4607ju[25];
extern const uint8_t gm_ssf1r8m922l8a[16];
extern const uint8_t gm_ssfg877iegnma[4];
extern const uint8_t gm_ssgnvuivrkqi2[84];
extern const uint8_t gm_ssj9o3d4o7kbv[43];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_ssn1v6q1cq4so[3];
extern const uint8_t gm_sso7iusdc4q0g[11];
extern const uint8_t gm_sspbbn4skfqau[8];
extern const uint8_t gm_sspmmsm16lqb6[29];
extern const uint8_t gm_sspppdgt1j7r3[29];
extern const uint8_t gm_ssqg2h6hrn099[9];
extern const uint8_t gm_ssscpugolo2j1[37];
extern const uint8_t gm_sstr4ffsnqfee[6];
extern const uint8_t gm_ssvpi8gpvt9jm[10];
extern const uint8_t gm_st027n235d4us[45];
extern const uint8_t gm_st0caaee167rh[24];
extern const uint8_t gm_st38p0j1i82np[11];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_st5nnur2j246q[36];
extern const uint8_t gm_st5oflrv0tmc6[21];
extern const uint8_t gm_st62vtcrpam9i[17];
extern const uint8_t gm_st7pul9bg6mqu[8];
extern const uint8_t gm_st87qih3spnnu[15];
extern const uint8_t gm_st943craifob3[31];
extern const uint8_t gm_st9gkvtvi92qe[17];
extern const uint8_t gm_stabrbc330nqe[18];
extern const uint8_t gm_stapduvfaru1r[68];
extern const uint8_t gm_stc3e0j0moc2h[5];
extern const uint8_t gm_stclkv4qnta30[9];
extern const uint8_t gm_std3g89n1j9ln[8];
extern const uint8_t gm_std91rqsr8j0b[7];
extern const uint8_t gm_stdh0kbfgj33k[9];
extern const uint8_t gm_stfor4qir1ijs[14];
extern const uint8_t gm_stgk573j9ijrq[41];
extern const uint8_t gm_sthtklncdroln[3];
extern const uint8_t gm_stn0r86gl3sc3[13];
extern const uint8_t gm_stott1n61n0gf[23];
extern const uint8_t gm_stotuneqhj37e[36];
extern const uint8_t gm_stpdu6olnph3l[33];
extern const uint8_t gm_stpevuujkc7eh[113];
extern const uint8_t gm_stpus1r2o4u5q[14];
extern const uint8_t gm_stqiimqtsnd8d[9];
extern const uint8_t gm_stt0il17c6uj9[17];
extern const uint8_t gm_sttp5u65omp7t[15];
extern const uint8_t gm_stvgh52bfcgit[52];
extern const uint8_t gm_su03its4kclfl[10];
extern const uint8_t gm_su0qghe21d3de[26];
extern const uint8_t gm_su2ndlglvt2c0[11];
extern const uint8_t gm_su2vudghakb3u[55];
extern const uint8_t gm_su30fl1m1ebg6[5];
extern const uint8_t gm_su42nkrseeged[18];
extern const uint8_t gm_su71itml70ikf[5];
extern const uint8_t gm_su7ft5rc766n4[3];
extern const uint8_t gm_su857f8o8i6a0[13];
extern const uint8_t gm_su8ulebf2ijla[20];
extern const uint8_t gm_sua89kt0f2v9n[30];
extern const uint8_t gm_suatl193icom4[41];
extern const uint8_t gm_sub7l8h1sjc33[12];
extern const uint8_t gm_suf3ai1bhvntt[8];
extern const uint8_t gm_sufbh0g4mbom0[11];
extern const uint8_t gm_sufoqrq4onvgh[16];
extern const uint8_t gm_suftpl3n8736g[5];
extern const uint8_t gm_suifusi0pb6s0[12];
extern const uint8_t gm_sujvknhnh5am8[19];
extern const uint8_t gm_suketc630ddb9[61];
extern const uint8_t gm_sumd85rpgjjrv[9];
extern const uint8_t gm_supi6mhbcvtna[5];
extern const uint8_t gm_supobfk7sjp99[48];
extern const uint8_t gm_suvf0cqih9u7i[17];
extern const uint8_t gm_suvpgtuh49tp7[20];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_sv8qamggba7e1[18];
extern const uint8_t gm_sv9j89ufns35b[16];
extern const uint8_t gm_svd1idu6ksvnb[90];
extern const uint8_t gm_svg70al9ibq8e[56];
extern const uint8_t gm_svhe917rela4j[20];
extern const uint8_t gm_svksed7nkdegg[25];
extern const uint8_t gm_svl4l2v56ma30[8];
extern const uint8_t gm_svld8cc4ptomt[16];
extern const uint8_t gm_svocs6aii9rqg[75];
extern const uint8_t gm_svt8aldhoetro[3];
extern const uint8_t gm_svulttobk4q7t[4];
extern const uint8_t gm_svv63hse0pejb[18];
extern const uint8_t gm_svvuql4te1jp4[9];
extern const gm_tmpl gm_tpl005eoei82778;
extern const uint32_t gm_tpl005eoei82778_i[];
extern const gm_tmpl_piece gm_tpl005eoei82778_p[];
extern const gm_spec *const gm_tpl005eoei82778_s[];
extern const gm_tmpl_piece gm_tpl00b7kgp37ng7_p[];
extern const uint32_t gm_tpl00nmcu166crh_i[];
extern const uint32_t gm_tpl00rv7vuig9ur_i[];
extern const uint32_t gm_tpl04hr2npsr626_i[];
extern const gm_tmpl_piece gm_tpl05osb04qe3ad_p[];
extern const gm_tmpl gm_tpl0c7sat2t7j6v;
extern const uint32_t gm_tpl0c7sat2t7j6v_i[];
extern const gm_tmpl_piece gm_tpl0c7sat2t7j6v_p[];
extern const gm_spec *const gm_tpl0c7sat2t7j6v_s[];
extern const gm_tmpl_piece gm_tpl0cgce8migcnp_p[];
extern const gm_tmpl gm_tpl0e2i60vm7gtr;
extern const uint32_t gm_tpl0e2i60vm7gtr_i[];
extern const gm_tmpl_piece gm_tpl0e2i60vm7gtr_p[];
extern const gm_spec *const gm_tpl0e2i60vm7gtr_s[];
extern const gm_tmpl_piece gm_tpl0e782j7cr191_p[];
extern const uint32_t gm_tpl0eln6evo1vf9_i[];
extern const gm_tmpl gm_tpl0euitr9hdia5;
extern const uint32_t gm_tpl0euitr9hdia5_i[];
extern const gm_tmpl_piece gm_tpl0euitr9hdia5_p[];
extern const gm_spec *const gm_tpl0euitr9hdia5_s[];
extern const uint32_t gm_tpl0fhs0t9qdf1o_i[];
extern const gm_tmpl gm_tpl0n7kmejjk8it;
extern const uint32_t gm_tpl0n7kmejjk8it_i[];
extern const gm_tmpl_piece gm_tpl0n7kmejjk8it_p[];
extern const gm_spec *const gm_tpl0n7kmejjk8it_s[];
extern const gm_tmpl gm_tpl0q3rsndpu857;
extern const uint32_t gm_tpl0q3rsndpu857_i[];
extern const gm_tmpl_piece gm_tpl0q3rsndpu857_p[];
extern const gm_spec *const gm_tpl0q3rsndpu857_s[];
extern const gm_tmpl_piece gm_tpl0sb47rg6hcij_p[];
extern const gm_spec *const gm_tpl0stjtt2gdn50_s[];
extern const uint32_t gm_tpl0tljhf3n7k2p_i[];
extern const gm_tmpl_piece gm_tpl0tr163o4bba1_p[];
extern const uint32_t gm_tpl0uott3s3afvp_i[];
extern const gm_tmpl gm_tpl0v2pofusugn6;
extern const uint32_t gm_tpl0v2pofusugn6_i[];
extern const gm_tmpl_piece gm_tpl0v2pofusugn6_p[];
extern const gm_spec *const gm_tpl0v2pofusugn6_s[];
extern const gm_tmpl_piece gm_tpl13591hp4g6qh_p[];
extern const gm_tmpl_piece gm_tpl14n6ndgi62j1_p[];
extern const gm_spec *const gm_tpl16gsv91msflc_s[];
extern const gm_tmpl gm_tpl16petnrgdcd0;
extern const uint32_t gm_tpl16petnrgdcd0_i[];
extern const gm_tmpl_piece gm_tpl16petnrgdcd0_p[];
extern const gm_spec *const gm_tpl16petnrgdcd0_s[];
extern const uint32_t gm_tpl17t0pmu1kh1u_i[];
extern const gm_spec *const gm_tpl1bh0s5ll1m9j_s[];
extern const gm_spec *const gm_tpl1buo4rn7srlb_s[];
extern const gm_tmpl gm_tpl1f1njss89nj4;
extern const uint32_t gm_tpl1f1njss89nj4_i[];
extern const gm_tmpl_piece gm_tpl1f1njss89nj4_p[];
extern const gm_spec *const gm_tpl1f1njss89nj4_s[];
extern const uint32_t gm_tpl1lvejpvpq0qh_i[];
extern const gm_spec *const gm_tpl1os40ii1iecn_s[];
extern const gm_spec *const gm_tpl1qq6rf1tb5ds_s[];
extern const gm_spec *const gm_tpl1r51q3hoe535_s[];
extern const gm_tmpl gm_tpl1t0vlt7tq67r;
extern const uint32_t gm_tpl1t0vlt7tq67r_i[];
extern const gm_tmpl_piece gm_tpl1t0vlt7tq67r_p[];
extern const gm_spec *const gm_tpl1t0vlt7tq67r_s[];
extern const uint32_t gm_tpl1u24a06iag0n_i[];
extern const gm_tmpl_piece gm_tpl1u8koiam5nmo_p[];
extern const uint32_t gm_tpl1vbgcviqausd_i[];
extern const gm_spec *const gm_tpl24fci6dm83eo_s[];
extern const gm_tmpl_piece gm_tpl265m7jpg5oed_p[];
extern const gm_spec *const gm_tpl265m7jpg5oed_s[];
extern const gm_spec *const gm_tpl29spsi1atllj_s[];
extern const uint32_t gm_tpl2a7pmi7aq67m_i[];
extern const gm_tmpl_piece gm_tpl2arnv1ofugei_p[];
extern const gm_spec *const gm_tpl2arnv1ofugei_s[];
extern const uint32_t gm_tpl2dia2gbnfh9l_i[];
extern const uint32_t gm_tpl2fcbm03ka11j_i[];
extern const gm_tmpl_piece gm_tpl2g2o9ile1980_p[];
extern const gm_tmpl gm_tpl2r84u6vnc58k;
extern const uint32_t gm_tpl2r84u6vnc58k_i[];
extern const gm_tmpl_piece gm_tpl2r84u6vnc58k_p[];
extern const gm_spec *const gm_tpl2r84u6vnc58k_s[];
extern const gm_tmpl_piece gm_tpl2re1r0fre48m_p[];
extern const gm_tmpl gm_tpl2u97uspakqd9;
extern const uint32_t gm_tpl2u97uspakqd9_i[];
extern const gm_tmpl_piece gm_tpl2u97uspakqd9_p[];
extern const gm_spec *const gm_tpl2u97uspakqd9_s[];
extern const uint32_t gm_tpl3052vn0bjbcl_i[];
extern const gm_spec *const gm_tpl32o7ct8vibbi_s[];
extern const gm_spec *const gm_tpl335dut97l9dl_s[];
extern const gm_spec *const gm_tpl3bnq2anocte9_s[];
extern const gm_tmpl gm_tpl3fu53qt58qjh;
extern const uint32_t gm_tpl3fu53qt58qjh_i[];
extern const gm_tmpl_piece gm_tpl3fu53qt58qjh_p[];
extern const gm_spec *const gm_tpl3fu53qt58qjh_s[];
extern const gm_tmpl gm_tpl3jlrgt0cfgj3;
extern const uint32_t gm_tpl3jlrgt0cfgj3_i[];
extern const gm_tmpl_piece gm_tpl3jlrgt0cfgj3_p[];
extern const gm_spec *const gm_tpl3jlrgt0cfgj3_s[];
extern const gm_tmpl gm_tpl3lqpj3d2egtb;
extern const uint32_t gm_tpl3lqpj3d2egtb_i[];
extern const gm_tmpl_piece gm_tpl3lqpj3d2egtb_p[];
extern const gm_spec *const gm_tpl3lqpj3d2egtb_s[];
extern const gm_spec *const gm_tpl3q3m44l3pqqn_s[];
extern const gm_tmpl_piece gm_tpl3sv6aqbksrr5_p[];
extern const gm_tmpl_piece gm_tpl3tcah2c39gjk_p[];
extern const uint32_t gm_tpl44b660pgg1c8_i[];
extern const gm_tmpl gm_tpl44cjgoji4tlu;
extern const uint32_t gm_tpl44cjgoji4tlu_i[];
extern const gm_tmpl_piece gm_tpl44cjgoji4tlu_p[];
extern const gm_spec *const gm_tpl44cjgoji4tlu_s[];
extern const uint32_t gm_tpl4561ar79q0c0_i[];
extern const gm_tmpl_piece gm_tpl4561ar79q0c0_p[];
extern const gm_spec *const gm_tpl470f2c4pe1su_s[];
extern const gm_tmpl gm_tpl4c2mfb3cngak;
extern const uint32_t gm_tpl4c2mfb3cngak_i[];
extern const gm_tmpl_piece gm_tpl4c2mfb3cngak_p[];
extern const gm_spec *const gm_tpl4c2mfb3cngak_s[];
extern const gm_spec *const gm_tpl4e4n6ul1t53g_s[];
extern const gm_spec *const gm_tpl4isjs1bg9fhg_s[];
extern const gm_tmpl gm_tpl4ivrecg9ncdu;
extern const uint32_t gm_tpl4ivrecg9ncdu_i[];
extern const gm_tmpl_piece gm_tpl4ivrecg9ncdu_p[];
extern const gm_spec *const gm_tpl4ivrecg9ncdu_s[];
extern const gm_tmpl gm_tpl4mhnbmvg0r80;
extern const uint32_t gm_tpl4mhnbmvg0r80_i[];
extern const gm_tmpl_piece gm_tpl4mhnbmvg0r80_p[];
extern const gm_spec *const gm_tpl4mhnbmvg0r80_s[];
extern const gm_spec *const gm_tpl4mli0f9rjihf_s[];
extern const gm_tmpl_piece gm_tpl4nfbp2ikscbk_p[];
extern const gm_tmpl gm_tpl4npct6bth1c7;
extern const uint32_t gm_tpl4npct6bth1c7_i[];
extern const gm_tmpl_piece gm_tpl4npct6bth1c7_p[];
extern const gm_spec *const gm_tpl4npct6bth1c7_s[];
extern const uint32_t gm_tpl4o9g8c43tino_i[];
extern const gm_spec *const gm_tpl4phv0vr9as7f_s[];
extern const gm_spec *const gm_tpl4tk9leh8nue4_s[];
extern const gm_spec *const gm_tpl52cv2e94esne_s[];
extern const gm_tmpl gm_tpl5536lp5da6eq;
extern const uint32_t gm_tpl5536lp5da6eq_i[];
extern const gm_tmpl_piece gm_tpl5536lp5da6eq_p[];
extern const gm_spec *const gm_tpl5536lp5da6eq_s[];
extern const gm_tmpl gm_tpl55mhnmub0ffl;
extern const uint32_t gm_tpl55mhnmub0ffl_i[];
extern const gm_tmpl_piece gm_tpl55mhnmub0ffl_p[];
extern const gm_spec *const gm_tpl55mhnmub0ffl_s[];
extern const gm_spec *const gm_tpl5677p7i476ao_s[];
extern const uint32_t gm_tpl570sljg0ivt5_i[];
extern const gm_tmpl gm_tpl59lqltdr0h0r;
extern const uint32_t gm_tpl59lqltdr0h0r_i[];
extern const gm_tmpl_piece gm_tpl59lqltdr0h0r_p[];
extern const gm_spec *const gm_tpl59lqltdr0h0r_s[];
extern const uint32_t gm_tpl5a8og2hcltt3_i[];
extern const gm_tmpl gm_tpl5c6n9e2iidjc;
extern const uint32_t gm_tpl5c6n9e2iidjc_i[];
extern const gm_tmpl_piece gm_tpl5c6n9e2iidjc_p[];
extern const gm_spec *const gm_tpl5c6n9e2iidjc_s[];
extern const uint32_t gm_tpl5cgoa6ddohcv_i[];
extern const uint32_t gm_tpl5d3ksa1ntdkn_i[];
extern const gm_spec *const gm_tpl5h4mc65ti3jt_s[];
extern const gm_tmpl gm_tpl5hatm6q530pn;
extern const uint32_t gm_tpl5hatm6q530pn_i[];
extern const gm_tmpl_piece gm_tpl5hatm6q530pn_p[];
extern const gm_spec *const gm_tpl5hatm6q530pn_s[];
extern const gm_spec *const gm_tpl5kl598jq5h12_s[];
extern const gm_spec *const gm_tpl5q9oi9aqgs43_s[];
extern const gm_tmpl_piece gm_tpl5uqj3jbiq5f0_p[];
extern const uint32_t gm_tpl65jqebqot15g_i[];
extern const gm_spec *const gm_tpl68oh02lfq7s5_s[];
extern const uint32_t gm_tpl69qlgb4nvkqp_i[];
extern const gm_tmpl_piece gm_tpl6bjoajp2fh2n_p[];
extern const gm_tmpl gm_tpl6fas3mugop2g;
extern const uint32_t gm_tpl6fas3mugop2g_i[];
extern const gm_tmpl_piece gm_tpl6fas3mugop2g_p[];
extern const gm_spec *const gm_tpl6fas3mugop2g_s[];
extern const gm_tmpl gm_tpl6gp425a13qgr;
extern const uint32_t gm_tpl6gp425a13qgr_i[];
extern const gm_tmpl_piece gm_tpl6gp425a13qgr_p[];
extern const gm_spec *const gm_tpl6gp425a13qgr_s[];
extern const gm_tmpl_piece gm_tpl6lldue1mm65c_p[];
extern const uint32_t gm_tpl6mra39d4fo2s_i[];
extern const gm_tmpl gm_tpl6pohii1e8pbg;
extern const uint32_t gm_tpl6pohii1e8pbg_i[];
extern const gm_tmpl_piece gm_tpl6pohii1e8pbg_p[];
extern const gm_spec *const gm_tpl6pohii1e8pbg_s[];
extern const gm_tmpl_piece gm_tpl6r9reg6pnruq_p[];
extern const gm_tmpl_piece gm_tpl6rgl0akrj87c_p[];
extern const gm_tmpl gm_tpl6s7ufou2uts6;
extern const uint32_t gm_tpl6s7ufou2uts6_i[];
extern const gm_tmpl_piece gm_tpl6s7ufou2uts6_p[];
extern const gm_spec *const gm_tpl6s7ufou2uts6_s[];
extern const uint32_t gm_tpl6skdtj6qlkmd_i[];
extern const gm_tmpl_piece gm_tpl6skdtj6qlkmd_p[];
extern const gm_tmpl_piece gm_tpl6tnfk0d9dl8e_p[];
extern const uint32_t gm_tpl6vfpiq020ehi_i[];
extern const gm_spec *const gm_tpl6vfpiq020ehi_s[];
extern const gm_tmpl gm_tpl726l5jbs5bmh;
extern const uint32_t gm_tpl726l5jbs5bmh_i[];
extern const gm_tmpl_piece gm_tpl726l5jbs5bmh_p[];
extern const gm_spec *const gm_tpl726l5jbs5bmh_s[];
extern const gm_tmpl gm_tpl737bm31hf71o;
extern const uint32_t gm_tpl737bm31hf71o_i[];
extern const gm_tmpl_piece gm_tpl737bm31hf71o_p[];
extern const gm_spec *const gm_tpl737bm31hf71o_s[];
extern const uint32_t gm_tpl77gpruiccsl2_i[];
extern const uint32_t gm_tpl7b1ibjg22p4i_i[];
extern const gm_spec *const gm_tpl7b1ibjg22p4i_s[];
extern const gm_tmpl_piece gm_tpl7b2kb2ainpff_p[];
extern const gm_tmpl gm_tpl7b4l0929mgk1;
extern const uint32_t gm_tpl7b4l0929mgk1_i[];
extern const gm_tmpl_piece gm_tpl7b4l0929mgk1_p[];
extern const gm_spec *const gm_tpl7b4l0929mgk1_s[];
extern const uint32_t gm_tpl7cjap0b34jjm_i[];
extern const gm_spec *const gm_tpl7m3t7r6fqpf7_s[];
extern const uint32_t gm_tpl7ut0r685hcaa_i[];
extern const gm_tmpl gm_tpl810t9tkid6i2;
extern const uint32_t gm_tpl810t9tkid6i2_i[];
extern const gm_tmpl_piece gm_tpl810t9tkid6i2_p[];
extern const gm_spec *const gm_tpl810t9tkid6i2_s[];
extern const gm_tmpl_piece gm_tpl81nh98hgo1rk_p[];
extern const gm_spec *const gm_tpl81nh98hgo1rk_s[];
extern const uint32_t gm_tpl81nunf5qvo6u_i[];
extern const uint32_t gm_tpl81uel28fst2a_i[];
extern const gm_spec *const gm_tpl82mi3mv6t0is_s[];
extern const gm_tmpl_piece gm_tpl82nt5tav1er7_p[];
extern const uint32_t gm_tpl84co505h34ud_i[];
extern const gm_tmpl_piece gm_tpl85lm1gq83hlb_p[];
extern const gm_tmpl gm_tpl85p2q6ermgga;
extern const uint32_t gm_tpl85p2q6ermgga_i[];
extern const gm_tmpl_piece gm_tpl85p2q6ermgga_p[];
extern const gm_spec *const gm_tpl85p2q6ermgga_s[];
extern const gm_spec *const gm_tpl896mu4np77il_s[];
extern const uint32_t gm_tpl8geisveh2vug_i[];
extern const gm_spec *const gm_tpl8geisveh2vug_s[];
extern const uint32_t gm_tpl8gj5o7rmo2fl_i[];
extern const gm_tmpl_piece gm_tpl8nmc259qeh4g_p[];
extern const gm_tmpl_piece gm_tpl8nqn0hncnfd4_p[];
extern const gm_tmpl_piece gm_tpl8pvdc7qbgge6_p[];
extern const uint32_t gm_tpl8ro9atn5p92f_i[];
extern const uint32_t gm_tpl8un0qv11eeom_i[];
extern const uint32_t gm_tpl8v9uhia7uc1b_i[];
extern const uint32_t gm_tpl9225uv77leci_i[];
extern const gm_tmpl_piece gm_tpl9225uv77leci_p[];
extern const gm_tmpl gm_tpl98as72l6ukas;
extern const uint32_t gm_tpl98as72l6ukas_i[];
extern const gm_tmpl_piece gm_tpl98as72l6ukas_p[];
extern const gm_spec *const gm_tpl98as72l6ukas_s[];
extern const gm_tmpl gm_tpl9900stiqclaj;
extern const uint32_t gm_tpl9900stiqclaj_i[];
extern const gm_tmpl_piece gm_tpl9900stiqclaj_p[];
extern const gm_spec *const gm_tpl9900stiqclaj_s[];
extern const gm_tmpl_piece gm_tpl9bim66fl3b37_p[];
extern const gm_spec *const gm_tpl9cnobsjbh0r2_s[];
extern const gm_spec *const gm_tpl9ljuo8s7738q_s[];
extern const gm_tmpl_piece gm_tpl9p5406jtlms7_p[];
extern const gm_tmpl gm_tpl9qc0voknsp7g;
extern const uint32_t gm_tpl9qc0voknsp7g_i[];
extern const gm_tmpl_piece gm_tpl9qc0voknsp7g_p[];
extern const gm_spec *const gm_tpl9qc0voknsp7g_s[];
extern const gm_spec *const gm_tpl9s09hqjpb581_s[];
extern const gm_spec *const gm_tpl9v7lb7n4a602_s[];
extern const gm_tmpl gm_tpl9vu8tqcv1d9s;
extern const uint32_t gm_tpl9vu8tqcv1d9s_i[];
extern const gm_tmpl_piece gm_tpl9vu8tqcv1d9s_p[];
extern const gm_spec *const gm_tpl9vu8tqcv1d9s_s[];
extern const gm_spec *const gm_tpla15qb3qedeuc_s[];
extern const gm_tmpl gm_tpla5rqmr49dnnk;
extern const uint32_t gm_tpla5rqmr49dnnk_i[];
extern const gm_tmpl_piece gm_tpla5rqmr49dnnk_p[];
extern const gm_spec *const gm_tpla5rqmr49dnnk_s[];
extern const gm_tmpl gm_tpla6b7o385jo3o;
extern const uint32_t gm_tpla6b7o385jo3o_i[];
extern const gm_tmpl_piece gm_tpla6b7o385jo3o_p[];
extern const gm_spec *const gm_tpla6b7o385jo3o_s[];
extern const gm_tmpl gm_tpla6rojfclgc54;
extern const uint32_t gm_tpla6rojfclgc54_i[];
extern const gm_tmpl_piece gm_tpla6rojfclgc54_p[];
extern const gm_spec *const gm_tpla6rojfclgc54_s[];
extern const gm_tmpl_piece gm_tpla843kfhqtvit_p[];
extern const gm_tmpl gm_tpla8nvlakostko;
extern const uint32_t gm_tpla8nvlakostko_i[];
extern const gm_tmpl_piece gm_tpla8nvlakostko_p[];
extern const gm_spec *const gm_tpla8nvlakostko_s[];
extern const gm_tmpl gm_tplacjeuo4at0jj;
extern const uint32_t gm_tplacjeuo4at0jj_i[];
extern const gm_tmpl_piece gm_tplacjeuo4at0jj_p[];
extern const gm_spec *const gm_tplacjeuo4at0jj_s[];
extern const uint32_t gm_tplacjkb64s2e83_i[];
extern const gm_spec *const gm_tplacjkb64s2e83_s[];
extern const gm_tmpl gm_tplae6k9mv0s1dm;
extern const uint32_t gm_tplae6k9mv0s1dm_i[];
extern const gm_tmpl_piece gm_tplae6k9mv0s1dm_p[];
extern const gm_spec *const gm_tplae6k9mv0s1dm_s[];
extern const uint32_t gm_tplamm5t71udd8k_i[];
extern const gm_spec *const gm_tplaq7jlj8i0s87_s[];
extern const uint32_t gm_tplaqje7ooa5hcl_i[];
extern const gm_tmpl_piece gm_tplaqje7ooa5hcl_p[];
extern const uint32_t gm_tplar04pgor24ut_i[];
extern const gm_tmpl_piece gm_tplau4cbc1piuvn_p[];
extern const gm_spec *const gm_tplav8bdvvt8rem_s[];
extern const gm_tmpl gm_tplb2gsdj8iioqj;
extern const uint32_t gm_tplb2gsdj8iioqj_i[];
extern const gm_tmpl_piece gm_tplb2gsdj8iioqj_p[];
extern const gm_spec *const gm_tplb2gsdj8iioqj_s[];
extern const uint32_t gm_tplb5ldvki5nntc_i[];
extern const gm_spec *const gm_tplb86t2p96msdt_s[];
extern const uint32_t gm_tplba0kefu5gu2c_i[];
extern const gm_spec *const gm_tplbbopfj3f21eb_s[];
extern const gm_spec *const gm_tplbdrnrpalngql_s[];
extern const gm_tmpl gm_tplbeuo81m3df3c;
extern const uint32_t gm_tplbeuo81m3df3c_i[];
extern const gm_tmpl_piece gm_tplbeuo81m3df3c_p[];
extern const gm_spec *const gm_tplbeuo81m3df3c_s[];
extern const gm_spec *const gm_tplbjh9dr2llnkb_s[];
extern const gm_tmpl_piece gm_tplbmcpcu1c6osb_p[];
extern const gm_spec *const gm_tplbmcpcu1c6osb_s[];
extern const gm_tmpl gm_tplbmf4neusqbo5;
extern const uint32_t gm_tplbmf4neusqbo5_i[];
extern const gm_tmpl_piece gm_tplbmf4neusqbo5_p[];
extern const gm_spec *const gm_tplbmf4neusqbo5_s[];
extern const gm_spec *const gm_tplbn5t5stpvru2_s[];
extern const gm_tmpl_piece gm_tplbo9rmalpf2vj_p[];
extern const gm_tmpl_piece gm_tplbqqnfekph537_p[];
extern const uint32_t gm_tplbr5d1kr4l84u_i[];
extern const gm_spec *const gm_tplbtqgdimqhkr1_s[];
extern const uint32_t gm_tplbulo51k20om4_i[];
extern const gm_tmpl gm_tplc5cre0uqposd;
extern const uint32_t gm_tplc5cre0uqposd_i[];
extern const gm_tmpl_piece gm_tplc5cre0uqposd_p[];
extern const gm_spec *const gm_tplc5cre0uqposd_s[];
extern const gm_spec *const gm_tplc65rc46hqohp_s[];
extern const gm_spec *const gm_tplcb1495nujsb7_s[];
extern const gm_tmpl_piece gm_tplcein5d5pq2cs_p[];
extern const uint32_t gm_tplceqrbdtcd9p5_i[];
extern const gm_tmpl gm_tplcilp3mvhfo4n;
extern const uint32_t gm_tplcilp3mvhfo4n_i[];
extern const gm_tmpl_piece gm_tplcilp3mvhfo4n_p[];
extern const gm_spec *const gm_tplcilp3mvhfo4n_s[];
extern const gm_tmpl gm_tplck4jpb6nd99p;
extern const uint32_t gm_tplck4jpb6nd99p_i[];
extern const gm_tmpl_piece gm_tplck4jpb6nd99p_p[];
extern const gm_spec *const gm_tplck4jpb6nd99p_s[];
extern const gm_spec *const gm_tplcte0qqjaa0q0_s[];
extern const gm_tmpl gm_tpld1ficc6ikf1e;
extern const uint32_t gm_tpld1ficc6ikf1e_i[];
extern const gm_tmpl_piece gm_tpld1ficc6ikf1e_p[];
extern const gm_spec *const gm_tpld1ficc6ikf1e_s[];
extern const uint32_t gm_tpldbe9bnjeqeqg_i[];
extern const gm_spec *const gm_tpldc3nmdc0cit1_s[];
extern const gm_tmpl_piece gm_tpldff5gvnhne6f_p[];
extern const gm_spec *const gm_tpldhof7u5v5b2c_s[];
extern const uint32_t gm_tpldiv6dh0or1km_i[];
extern const gm_tmpl_piece gm_tpldknbfev12jrb_p[];
extern const uint32_t gm_tpldm28gt81dafq_i[];
extern const gm_spec *const gm_tpldm28gt81dafq_s[];
extern const gm_tmpl gm_tpldp3bk13rlc44;
extern const uint32_t gm_tpldp3bk13rlc44_i[];
extern const gm_tmpl_piece gm_tpldp3bk13rlc44_p[];
extern const gm_spec *const gm_tpldp3bk13rlc44_s[];
extern const uint32_t gm_tpldqjt9kdk4ntt_i[];
extern const gm_tmpl_piece gm_tpldqrk6ll2jb23_p[];
extern const gm_tmpl_piece gm_tpldu1003b7br77_p[];
extern const gm_spec *const gm_tpldunbbu9dpisn_s[];
extern const gm_tmpl gm_tpldvh3e1jcen1u;
extern const uint32_t gm_tpldvh3e1jcen1u_i[];
extern const gm_tmpl_piece gm_tpldvh3e1jcen1u_p[];
extern const gm_spec *const gm_tpldvh3e1jcen1u_s[];
extern const uint32_t gm_tple41tg2t64d1i_i[];
extern const gm_spec *const gm_tple41tg2t64d1i_s[];
extern const gm_spec *const gm_tple4upi9cflnq6_s[];
extern const gm_tmpl gm_tple7i0r0sg6sjs;
extern const uint32_t gm_tple7i0r0sg6sjs_i[];
extern const gm_tmpl_piece gm_tple7i0r0sg6sjs_p[];
extern const gm_spec *const gm_tple7i0r0sg6sjs_s[];
extern const gm_tmpl_piece gm_tplec2d6tt24j0q_p[];
extern const gm_tmpl gm_tpled61een8fpfm;
extern const uint32_t gm_tpled61een8fpfm_i[];
extern const gm_tmpl_piece gm_tpled61een8fpfm_p[];
extern const gm_spec *const gm_tpled61een8fpfm_s[];
extern const gm_tmpl_piece gm_tplegelorakvcn7_p[];
extern const gm_spec *const gm_tplem02qig8laoi_s[];
extern const gm_tmpl_piece gm_tplenatqap1hjoj_p[];
extern const gm_spec *const gm_tpleo7934k1qvrc_s[];
extern const gm_spec *const gm_tpleoabj1u2rb54_s[];
extern const gm_tmpl gm_tplf1r4lr6ofdql;
extern const uint32_t gm_tplf1r4lr6ofdql_i[];
extern const gm_tmpl_piece gm_tplf1r4lr6ofdql_p[];
extern const gm_spec *const gm_tplf1r4lr6ofdql_s[];
extern const gm_spec *const gm_tplf28d52hmmd2n_s[];
extern const gm_tmpl_piece gm_tplf3c2iulsht2e_p[];
extern const uint32_t gm_tplf41j0oji3nkl_i[];
extern const gm_tmpl_piece gm_tplf6770ieb5fa8_p[];
extern const gm_spec *const gm_tplf8er2klne616_s[];
extern const uint32_t gm_tplf9434bnahqi1_i[];
extern const gm_spec *const gm_tplf9q60in6m019_s[];
extern const gm_tmpl_piece gm_tplfft5kohcphn8_p[];
extern const gm_tmpl_piece gm_tplfg7j0v7js1f7_p[];
extern const gm_spec *const gm_tplfjua3t6jsoe6_s[];
extern const gm_tmpl gm_tplfkugp8hqqhvu;
extern const uint32_t gm_tplfkugp8hqqhvu_i[];
extern const gm_tmpl_piece gm_tplfkugp8hqqhvu_p[];
extern const gm_spec *const gm_tplfkugp8hqqhvu_s[];
extern const gm_tmpl_piece gm_tplfo19v5itil8t_p[];
extern const uint32_t gm_tplfqk35ejq39la_i[];
extern const uint32_t gm_tplfqr816g9de0k_i[];
extern const uint32_t gm_tplfrm74l83ht8l_i[];
extern const gm_tmpl_piece gm_tplfsmasr3tgalo_p[];
extern const gm_tmpl_piece gm_tplft0ecs37svcn_p[];
extern const gm_spec *const gm_tplft0ecs37svcn_s[];
extern const uint32_t gm_tplful0p0r5pifs_i[];
extern const gm_tmpl gm_tplg0rms0t7302m;
extern const uint32_t gm_tplg0rms0t7302m_i[];
extern const gm_tmpl_piece gm_tplg0rms0t7302m_p[];
extern const gm_spec *const gm_tplg0rms0t7302m_s[];
extern const gm_tmpl_piece gm_tplg24lfmv0pffq_p[];
extern const gm_spec *const gm_tplg2qlrjk1sunc_s[];
extern const gm_tmpl gm_tplg45fl0f0qtll;
extern const uint32_t gm_tplg45fl0f0qtll_i[];
extern const gm_tmpl_piece gm_tplg45fl0f0qtll_p[];
extern const gm_spec *const gm_tplg45fl0f0qtll_s[];
extern const gm_tmpl gm_tplg45jfs21ci2c;
extern const uint32_t gm_tplg45jfs21ci2c_i[];
extern const gm_tmpl_piece gm_tplg45jfs21ci2c_p[];
extern const gm_spec *const gm_tplg45jfs21ci2c_s[];
extern const gm_tmpl gm_tplgemvjdfjljaa;
extern const uint32_t gm_tplgemvjdfjljaa_i[];
extern const gm_tmpl_piece gm_tplgemvjdfjljaa_p[];
extern const gm_spec *const gm_tplgemvjdfjljaa_s[];
extern const gm_tmpl_piece gm_tplggs57b0p2or4_p[];
extern const gm_tmpl_piece gm_tplgja2nber62dc_p[];
extern const gm_tmpl_piece gm_tplgjqa4ejl8op1_p[];
extern const gm_spec *const gm_tplgjqa4ejl8op1_s[];
extern const gm_tmpl_piece gm_tplgnp9u7qeb3h6_p[];
extern const uint32_t gm_tplgp08d1iih1si_i[];
extern const gm_tmpl gm_tplgqohfqetaph7;
extern const uint32_t gm_tplgqohfqetaph7_i[];
extern const gm_tmpl_piece gm_tplgqohfqetaph7_p[];
extern const gm_spec *const gm_tplgqohfqetaph7_s[];
extern const gm_tmpl_piece gm_tplgvu725182gad_p[];
extern const gm_tmpl gm_tplh2em7r1dmffm;
extern const uint32_t gm_tplh2em7r1dmffm_i[];
extern const gm_tmpl_piece gm_tplh2em7r1dmffm_p[];
extern const gm_spec *const gm_tplh2em7r1dmffm_s[];
extern const gm_tmpl gm_tplh78gsuiavvas;
extern const uint32_t gm_tplh78gsuiavvas_i[];
extern const gm_tmpl_piece gm_tplh78gsuiavvas_p[];
extern const gm_spec *const gm_tplh78gsuiavvas_s[];
extern const gm_tmpl gm_tplhatoh83mi4is;
extern const uint32_t gm_tplhatoh83mi4is_i[];
extern const gm_tmpl_piece gm_tplhatoh83mi4is_p[];
extern const gm_spec *const gm_tplhatoh83mi4is_s[];
extern const gm_tmpl gm_tplhdb29uo8rm6j;
extern const uint32_t gm_tplhdb29uo8rm6j_i[];
extern const gm_tmpl_piece gm_tplhdb29uo8rm6j_p[];
extern const gm_spec *const gm_tplhdb29uo8rm6j_s[];
extern const gm_tmpl_piece gm_tplhgdgs8lv4h7d_p[];
extern const gm_tmpl gm_tplhiqpct20jd67;
extern const uint32_t gm_tplhiqpct20jd67_i[];
extern const gm_tmpl_piece gm_tplhiqpct20jd67_p[];
extern const gm_spec *const gm_tplhiqpct20jd67_s[];
extern const gm_tmpl gm_tplhl4q6p2b5f92;
extern const uint32_t gm_tplhl4q6p2b5f92_i[];
extern const gm_tmpl_piece gm_tplhl4q6p2b5f92_p[];
extern const gm_spec *const gm_tplhl4q6p2b5f92_s[];
extern const gm_tmpl_piece gm_tplhmairn7oibac_p[];
extern const gm_spec *const gm_tplhojm4dgu514i_s[];
extern const gm_tmpl gm_tplhr6emralh8q6;
extern const uint32_t gm_tplhr6emralh8q6_i[];
extern const gm_tmpl_piece gm_tplhr6emralh8q6_p[];
extern const gm_spec *const gm_tplhr6emralh8q6_s[];
extern const gm_tmpl_piece gm_tplhrko1gs9n6tk_p[];
extern const uint32_t gm_tplhrqv5p3pfbh5_i[];
extern const uint32_t gm_tpli08vvf1bckj4_i[];
extern const uint32_t gm_tpli2vu74n9ss6f_i[];
extern const uint32_t gm_tpli7ohq5o5sj5n_i[];
extern const gm_tmpl_piece gm_tpli824iv2hf4o6_p[];
extern const gm_tmpl_piece gm_tplibksf6f0e00q_p[];
extern const gm_tmpl_piece gm_tpliig5tr0gjn5a_p[];
extern const gm_tmpl gm_tpliq8p2spcvq06;
extern const uint32_t gm_tpliq8p2spcvq06_i[];
extern const gm_tmpl_piece gm_tpliq8p2spcvq06_p[];
extern const gm_spec *const gm_tpliq8p2spcvq06_s[];
extern const gm_tmpl gm_tpliqi7pna59a9o;
extern const uint32_t gm_tpliqi7pna59a9o_i[];
extern const gm_tmpl_piece gm_tpliqi7pna59a9o_p[];
extern const gm_spec *const gm_tpliqi7pna59a9o_s[];
extern const gm_spec *const gm_tpliqpvgtpo05sn_s[];
extern const gm_tmpl gm_tpliuif0nifmui1;
extern const uint32_t gm_tpliuif0nifmui1_i[];
extern const gm_tmpl_piece gm_tpliuif0nifmui1_p[];
extern const gm_spec *const gm_tpliuif0nifmui1_s[];
extern const gm_spec *const gm_tplivip1s1762gt_s[];
extern const gm_spec *const gm_tplj16n1o03849r_s[];
extern const uint32_t gm_tplj451rojbvr15_i[];
extern const gm_spec *const gm_tplj4gqj9p0fguq_s[];
extern const gm_spec *const gm_tplj7j0vuugbh2u_s[];
extern const gm_tmpl_piece gm_tplj972sj9sp61d_p[];
extern const gm_spec *const gm_tplja7aur1oiv70_s[];
extern const gm_tmpl gm_tpljd9codq72bc9;
extern const uint32_t gm_tpljd9codq72bc9_i[];
extern const gm_tmpl_piece gm_tpljd9codq72bc9_p[];
extern const gm_spec *const gm_tpljd9codq72bc9_s[];
extern const gm_tmpl_piece gm_tpljhar6jmbtsst_p[];
extern const uint32_t gm_tpljkde9cibn2er_i[];
extern const gm_tmpl_piece gm_tpljueetjft1ril_p[];
extern const uint32_t gm_tplk25d3iiaamil_i[];
extern const gm_spec *const gm_tplk3nkr2eh6don_s[];
extern const gm_spec *const gm_tplkaran1m5lpqm_s[];
extern const gm_tmpl gm_tplkd4irlmplk3e;
extern const uint32_t gm_tplkd4irlmplk3e_i[];
extern const gm_tmpl_piece gm_tplkd4irlmplk3e_p[];
extern const gm_spec *const gm_tplkd4irlmplk3e_s[];
extern const gm_spec *const gm_tplkfag0fic60vv_s[];
extern const gm_spec *const gm_tplkinjhvj7vqlj_s[];
extern const uint32_t gm_tplkjc1g73er3ub_i[];
extern const gm_spec *const gm_tplknfbq49i74sp_s[];
extern const gm_tmpl gm_tplkodpfg3ik1kh;
extern const uint32_t gm_tplkodpfg3ik1kh_i[];
extern const gm_tmpl_piece gm_tplkodpfg3ik1kh_p[];
extern const gm_spec *const gm_tplkodpfg3ik1kh_s[];
extern const gm_tmpl gm_tplksrq9kcn68vt;
extern const uint32_t gm_tplksrq9kcn68vt_i[];
extern const gm_tmpl_piece gm_tplksrq9kcn68vt_p[];
extern const gm_spec *const gm_tplksrq9kcn68vt_s[];
extern const uint32_t gm_tpll66vdkdoc48m_i[];
extern const gm_tmpl_piece gm_tpll6jntfkrhtt4_p[];
extern const uint32_t gm_tpll6pilv9krmhr_i[];
extern const gm_tmpl_piece gm_tpllaurt6qemv8q_p[];
extern const gm_spec *const gm_tpllbgi055uoiek_s[];
extern const gm_tmpl gm_tpllbu1ke0v3v7a;
extern const uint32_t gm_tpllbu1ke0v3v7a_i[];
extern const gm_tmpl_piece gm_tpllbu1ke0v3v7a_p[];
extern const gm_spec *const gm_tpllbu1ke0v3v7a_s[];
extern const gm_tmpl_piece gm_tplld08lp5kl07i_p[];
extern const uint32_t gm_tplld0dt78h0tum_i[];
extern const gm_tmpl gm_tpllf7nufk7lvkm;
extern const uint32_t gm_tpllf7nufk7lvkm_i[];
extern const gm_tmpl_piece gm_tpllf7nufk7lvkm_p[];
extern const gm_spec *const gm_tpllf7nufk7lvkm_s[];
extern const gm_tmpl_piece gm_tpllgrg03l2v74l_p[];
extern const gm_tmpl gm_tplli6rlvq5vnvb;
extern const uint32_t gm_tplli6rlvq5vnvb_i[];
extern const gm_tmpl_piece gm_tplli6rlvq5vnvb_p[];
extern const gm_spec *const gm_tplli6rlvq5vnvb_s[];
extern const gm_spec *const gm_tpllktp07diubcq_s[];
extern const gm_tmpl_piece gm_tpllqboe50ngs7g_p[];
extern const gm_spec *const gm_tplls1lesito750_s[];
extern const gm_tmpl_piece gm_tplm16s64mnihe4_p[];
extern const gm_tmpl gm_tplm2m182mt5ljj;
extern const uint32_t gm_tplm2m182mt5ljj_i[];
extern const gm_tmpl_piece gm_tplm2m182mt5ljj_p[];
extern const gm_spec *const gm_tplm2m182mt5ljj_s[];
extern const gm_tmpl_piece gm_tplm3a458dq976v_p[];
extern const gm_spec *const gm_tplm3a458dq976v_s[];
extern const gm_spec *const gm_tplm3dvc32djhr7_s[];
extern const gm_tmpl_piece gm_tplm9hj31tk9ip4_p[];
extern const gm_tmpl_piece gm_tplm9t4pej720h2_p[];
extern const gm_tmpl_piece gm_tplmcmsjqrp0d64_p[];
extern const uint32_t gm_tplmeuuf37dghpi_i[];
extern const uint32_t gm_tplmghq7vvcvnvi_i[];
extern const gm_spec *const gm_tplmiqlr2bu75gm_s[];
extern const uint32_t gm_tplmkb4jrr2jqel_i[];
extern const uint32_t gm_tplmm5mgnjalbfr_i[];
extern const gm_tmpl_piece gm_tplmn77qas0g7s4_p[];
extern const gm_tmpl gm_tplmnlel5aohei6;
extern const uint32_t gm_tplmnlel5aohei6_i[];
extern const gm_tmpl_piece gm_tplmnlel5aohei6_p[];
extern const gm_spec *const gm_tplmnlel5aohei6_s[];
extern const gm_spec *const gm_tplmo458jpb0n1e_s[];
extern const uint32_t gm_tplmpc7ghuokash_i[];
extern const gm_tmpl gm_tplmqflu0tp0eas;
extern const uint32_t gm_tplmqflu0tp0eas_i[];
extern const gm_tmpl_piece gm_tplmqflu0tp0eas_p[];
extern const gm_spec *const gm_tplmqflu0tp0eas_s[];
extern const gm_spec *const gm_tplmr9p697ovoo7_s[];
extern const gm_tmpl_piece gm_tplmsd0sfgf2v66_p[];
extern const gm_tmpl_piece gm_tplmv1ukn138isf_p[];
extern const gm_tmpl_piece gm_tpln1554he46gkc_p[];
extern const gm_tmpl gm_tpln2j7s58m8uan;
extern const uint32_t gm_tpln2j7s58m8uan_i[];
extern const gm_tmpl_piece gm_tpln2j7s58m8uan_p[];
extern const gm_spec *const gm_tpln2j7s58m8uan_s[];
extern const gm_tmpl gm_tpln38mjgenps81;
extern const uint32_t gm_tpln38mjgenps81_i[];
extern const gm_tmpl_piece gm_tpln38mjgenps81_p[];
extern const gm_spec *const gm_tpln38mjgenps81_s[];
extern const gm_spec *const gm_tpln4t9g9do4qg7_s[];
extern const gm_tmpl_piece gm_tpln5ioe1q518f0_p[];
extern const gm_spec *const gm_tpln7ptjfs3pum5_s[];
extern const uint32_t gm_tpln8gtl1e9t7j8_i[];
extern const uint32_t gm_tplnb9cl2s2ukch_i[];
extern const gm_spec *const gm_tplnbbj7t6ap7ht_s[];
extern const uint32_t gm_tplnc4en2s3t0da_i[];
extern const gm_tmpl gm_tplngknbnp9i821;
extern const uint32_t gm_tplngknbnp9i821_i[];
extern const gm_tmpl_piece gm_tplngknbnp9i821_p[];
extern const gm_spec *const gm_tplngknbnp9i821_s[];
extern const gm_tmpl_piece gm_tplnmm43n0jmcod_p[];
extern const gm_spec *const gm_tplnmm43n0jmcod_s[];
extern const gm_tmpl_piece gm_tplnrfprs01gfmj_p[];
extern const gm_tmpl gm_tplnrt3ito76b2v;
extern const uint32_t gm_tplnrt3ito76b2v_i[];
extern const gm_tmpl_piece gm_tplnrt3ito76b2v_p[];
extern const gm_spec *const gm_tplnrt3ito76b2v_s[];
extern const uint32_t gm_tplnt454lmhb7iv_i[];
extern const gm_tmpl_piece gm_tplo08f1kg3k97n_p[];
extern const gm_tmpl_piece gm_tplo6lbijqleti1_p[];
extern const gm_spec *const gm_tploblcdf1g38ca_s[];
extern const gm_tmpl gm_tplof36cm9vqs8e;
extern const uint32_t gm_tplof36cm9vqs8e_i[];
extern const gm_tmpl_piece gm_tplof36cm9vqs8e_p[];
extern const gm_spec *const gm_tplof36cm9vqs8e_s[];
extern const gm_spec *const gm_tploj4uaptg6qnk_s[];
extern const gm_tmpl gm_tplok9pv33vcsg0;
extern const uint32_t gm_tplok9pv33vcsg0_i[];
extern const gm_tmpl_piece gm_tplok9pv33vcsg0_p[];
extern const gm_spec *const gm_tplok9pv33vcsg0_s[];
extern const gm_tmpl gm_tplokvovhsdbec1;
extern const uint32_t gm_tplokvovhsdbec1_i[];
extern const gm_tmpl_piece gm_tplokvovhsdbec1_p[];
extern const gm_spec *const gm_tplokvovhsdbec1_s[];
extern const gm_tmpl gm_tplolonhuuitsj2;
extern const uint32_t gm_tplolonhuuitsj2_i[];
extern const gm_tmpl_piece gm_tplolonhuuitsj2_p[];
extern const gm_spec *const gm_tplolonhuuitsj2_s[];
extern const gm_spec *const gm_tplom6fga2t4kfv_s[];
extern const gm_tmpl gm_tplooqfb0umhrq0;
extern const uint32_t gm_tplooqfb0umhrq0_i[];
extern const gm_tmpl_piece gm_tplooqfb0umhrq0_p[];
extern const gm_spec *const gm_tplooqfb0umhrq0_s[];
extern const gm_spec *const gm_tplop2uhngiv88m_s[];
extern const uint32_t gm_tplorj2ck0ovl7t_i[];
extern const gm_tmpl_piece gm_tplotost4gd8bha_p[];
extern const gm_spec *const gm_tplovlajhkvntql_s[];
extern const gm_tmpl gm_tplp3l2s17nqpoi;
extern const uint32_t gm_tplp3l2s17nqpoi_i[];
extern const gm_tmpl_piece gm_tplp3l2s17nqpoi_p[];
extern const gm_spec *const gm_tplp3l2s17nqpoi_s[];
extern const gm_spec *const gm_tplp3rnngb19vkd_s[];
extern const uint32_t gm_tplp98fsht4k706_i[];
extern const gm_tmpl_piece gm_tplpav1vs8t9nu9_p[];
extern const uint32_t gm_tplpcjaq35mrruk_i[];
extern const gm_tmpl_piece gm_tplpd6kdal33co2_p[];
extern const gm_tmpl gm_tplpf7ckn15kjth;
extern const uint32_t gm_tplpf7ckn15kjth_i[];
extern const gm_tmpl_piece gm_tplpf7ckn15kjth_p[];
extern const gm_spec *const gm_tplpf7ckn15kjth_s[];
extern const gm_spec *const gm_tplpj1vfgivuj13_s[];
extern const gm_tmpl_piece gm_tplpkq4i4j83tmk_p[];
extern const uint32_t gm_tplptu6sgqf50o0_i[];
extern const gm_tmpl gm_tplq0gioeoiukeh;
extern const uint32_t gm_tplq0gioeoiukeh_i[];
extern const gm_tmpl_piece gm_tplq0gioeoiukeh_p[];
extern const gm_spec *const gm_tplq0gioeoiukeh_s[];
extern const gm_tmpl gm_tplq0mvos8lisnf;
extern const uint32_t gm_tplq0mvos8lisnf_i[];
extern const gm_tmpl_piece gm_tplq0mvos8lisnf_p[];
extern const gm_spec *const gm_tplq0mvos8lisnf_s[];
extern const gm_spec *const gm_tplq11qqgpg1tv1_s[];
extern const uint32_t gm_tplq54tg5m6aueh_i[];
extern const gm_tmpl gm_tplq75gqr5tvlmd;
extern const uint32_t gm_tplq75gqr5tvlmd_i[];
extern const gm_tmpl_piece gm_tplq75gqr5tvlmd_p[];
extern const gm_spec *const gm_tplq75gqr5tvlmd_s[];
extern const uint32_t gm_tplqci9e677opav_i[];
extern const gm_spec *const gm_tplqfgvjo31nom3_s[];
extern const uint32_t gm_tplqhugo4i82obv_i[];
extern const gm_tmpl gm_tplqjo10upi8bs6;
extern const uint32_t gm_tplqjo10upi8bs6_i[];
extern const gm_tmpl_piece gm_tplqjo10upi8bs6_p[];
extern const gm_spec *const gm_tplqjo10upi8bs6_s[];
extern const gm_tmpl_piece gm_tplqm5shuhajt4a_p[];
extern const uint32_t gm_tplqn6e8rqmlvpo_i[];
extern const gm_tmpl_piece gm_tplqn6e8rqmlvpo_p[];
extern const uint32_t gm_tplqpg9790rv4ki_i[];
extern const uint32_t gm_tplqpi0kdavqkt9_i[];
extern const gm_spec *const gm_tplqpi0kdavqkt9_s[];
extern const gm_tmpl gm_tplqrbhvccemc9k;
extern const uint32_t gm_tplqrbhvccemc9k_i[];
extern const gm_tmpl_piece gm_tplqrbhvccemc9k_p[];
extern const gm_spec *const gm_tplqrbhvccemc9k_s[];
extern const gm_tmpl_piece gm_tplr7vr7aubja28_p[];
extern const gm_spec *const gm_tplr8vkqgn7raqo_s[];
extern const uint32_t gm_tplr976guo57cat_i[];
extern const uint32_t gm_tplr9kq0pku58q3_i[];
extern const gm_tmpl gm_tplrbicm1oan2em;
extern const uint32_t gm_tplrbicm1oan2em_i[];
extern const gm_tmpl_piece gm_tplrbicm1oan2em_p[];
extern const gm_spec *const gm_tplrbicm1oan2em_s[];
extern const gm_tmpl gm_tplrd5hip684e7e;
extern const uint32_t gm_tplrd5hip684e7e_i[];
extern const gm_tmpl_piece gm_tplrd5hip684e7e_p[];
extern const gm_spec *const gm_tplrd5hip684e7e_s[];
extern const gm_tmpl gm_tplrffp652tkgo2;
extern const uint32_t gm_tplrffp652tkgo2_i[];
extern const gm_tmpl_piece gm_tplrffp652tkgo2_p[];
extern const gm_spec *const gm_tplrffp652tkgo2_s[];
extern const uint32_t gm_tplrgo7romk0g20_i[];
extern const gm_spec *const gm_tplrpouqnnj8vo1_s[];
extern const gm_tmpl gm_tplrtgv8em132rl;
extern const uint32_t gm_tplrtgv8em132rl_i[];
extern const gm_tmpl_piece gm_tplrtgv8em132rl_p[];
extern const gm_spec *const gm_tplrtgv8em132rl_s[];
extern const uint32_t gm_tplruf3ms5lqgst_i[];
extern const gm_tmpl gm_tpls5bjfpqmr7e0;
extern const uint32_t gm_tpls5bjfpqmr7e0_i[];
extern const gm_tmpl_piece gm_tpls5bjfpqmr7e0_p[];
extern const gm_spec *const gm_tpls5bjfpqmr7e0_s[];
extern const gm_tmpl gm_tpls85ptecv5rkj;
extern const uint32_t gm_tpls85ptecv5rkj_i[];
extern const gm_tmpl_piece gm_tpls85ptecv5rkj_p[];
extern const gm_spec *const gm_tpls85ptecv5rkj_s[];
extern const gm_tmpl_piece gm_tplsdokr8v76bhc_p[];
extern const gm_tmpl_piece gm_tplsf6r3t44km8l_p[];
extern const uint32_t gm_tplsil8jb9ng0ao_i[];
extern const gm_spec *const gm_tplsjufktgf4o1v_s[];
extern const uint32_t gm_tplsk9b8o9jgshf_i[];
extern const uint32_t gm_tplska2t1f86rr8_i[];
extern const gm_tmpl_piece gm_tplsleqso6g0tjq_p[];
extern const gm_tmpl gm_tplsmpaecrda5mc;
extern const uint32_t gm_tplsmpaecrda5mc_i[];
extern const gm_tmpl_piece gm_tplsmpaecrda5mc_p[];
extern const gm_spec *const gm_tplsmpaecrda5mc_s[];
extern const gm_tmpl gm_tplsns868vihk0u;
extern const uint32_t gm_tplsns868vihk0u_i[];
extern const gm_tmpl_piece gm_tplsns868vihk0u_p[];
extern const gm_spec *const gm_tplsns868vihk0u_s[];
extern const gm_tmpl_piece gm_tplso01g2paq6o6_p[];
extern const uint32_t gm_tplsogg2sp0m9p9_i[];
extern const gm_tmpl gm_tplsrr2cfplv9ff;
extern const uint32_t gm_tplsrr2cfplv9ff_i[];
extern const gm_tmpl_piece gm_tplsrr2cfplv9ff_p[];
extern const gm_spec *const gm_tplsrr2cfplv9ff_s[];
extern const uint32_t gm_tplt1lu36t604pk_i[];
extern const gm_tmpl gm_tplt320ru9bfpdd;
extern const uint32_t gm_tplt320ru9bfpdd_i[];
extern const gm_tmpl_piece gm_tplt320ru9bfpdd_p[];
extern const gm_spec *const gm_tplt320ru9bfpdd_s[];
extern const gm_spec *const gm_tplt3knh0ehirt7_s[];
extern const gm_spec *const gm_tplt5ks894658kl_s[];
extern const gm_spec *const gm_tplt7inuen2vfdh_s[];
extern const gm_tmpl gm_tpltktipp7m43tk;
extern const uint32_t gm_tpltktipp7m43tk_i[];
extern const gm_tmpl_piece gm_tpltktipp7m43tk_p[];
extern const gm_spec *const gm_tpltktipp7m43tk_s[];
extern const gm_tmpl gm_tpltue438lnncbq;
extern const uint32_t gm_tpltue438lnncbq_i[];
extern const gm_tmpl_piece gm_tpltue438lnncbq_p[];
extern const gm_spec *const gm_tpltue438lnncbq_s[];
extern const gm_spec *const gm_tplu0n2ra1s30o8_s[];
extern const uint32_t gm_tplu0rdj3ds7u0l_i[];
extern const gm_spec *const gm_tplu272587ugnvi_s[];
extern const gm_spec *const gm_tplu58b6rd4mq28_s[];
extern const gm_tmpl_piece gm_tplu65cpe6nhlbb_p[];
extern const gm_tmpl gm_tplu7oi9kuo0adg;
extern const uint32_t gm_tplu7oi9kuo0adg_i[];
extern const gm_tmpl_piece gm_tplu7oi9kuo0adg_p[];
extern const gm_spec *const gm_tplu7oi9kuo0adg_s[];
extern const gm_tmpl gm_tplua69gir871ue;
extern const uint32_t gm_tplua69gir871ue_i[];
extern const gm_tmpl_piece gm_tplua69gir871ue_p[];
extern const gm_spec *const gm_tplua69gir871ue_s[];
extern const uint32_t gm_tpludgprff7jihr_i[];
extern const gm_spec *const gm_tplue01enectvb0_s[];
extern const gm_tmpl gm_tplueje3nombrfc;
extern const uint32_t gm_tplueje3nombrfc_i[];
extern const gm_tmpl_piece gm_tplueje3nombrfc_p[];
extern const gm_spec *const gm_tplueje3nombrfc_s[];
extern const uint32_t gm_tpluf8termqcruk_i[];
extern const uint32_t gm_tplugm36655v1ns_i[];
extern const uint32_t gm_tpluguraohl9kn4_i[];
extern const gm_tmpl_piece gm_tplukjghf5ng2te_p[];
extern const uint32_t gm_tplun02iesa1r5k_i[];
extern const uint32_t gm_tplup0bf5d2eopf_i[];
extern const uint32_t gm_tplurleasos6378_i[];
extern const gm_tmpl gm_tpluttgpct24aib;
extern const uint32_t gm_tpluttgpct24aib_i[];
extern const gm_tmpl_piece gm_tpluttgpct24aib_p[];
extern const gm_spec *const gm_tpluttgpct24aib_s[];
extern const gm_spec *const gm_tpluuksm24afvp0_s[];
extern const gm_tmpl gm_tpluuts50bm11md;
extern const uint32_t gm_tpluuts50bm11md_i[];
extern const gm_tmpl_piece gm_tpluuts50bm11md_p[];
extern const gm_spec *const gm_tpluuts50bm11md_s[];
extern const gm_spec *const gm_tplv05t19d54qoj_s[];
extern const gm_spec *const gm_tplv1i7e9gejv6a_s[];
extern const gm_tmpl_piece gm_tplv23k7silrvnp_p[];
extern const gm_tmpl gm_tplv6dbvs9bjnes;
extern const uint32_t gm_tplv6dbvs9bjnes_i[];
extern const gm_tmpl_piece gm_tplv6dbvs9bjnes_p[];
extern const gm_spec *const gm_tplv6dbvs9bjnes_s[];
extern const gm_tmpl_piece gm_tplv8ckuk7p1s8m_p[];
extern const gm_tmpl gm_tplvbcfr0k6nmhv;
extern const uint32_t gm_tplvbcfr0k6nmhv_i[];
extern const gm_tmpl_piece gm_tplvbcfr0k6nmhv_p[];
extern const gm_spec *const gm_tplvbcfr0k6nmhv_s[];
extern const uint32_t gm_tplvcccam8nulr1_i[];
extern const gm_tmpl gm_tplvcuursopmobk;
extern const uint32_t gm_tplvcuursopmobk_i[];
extern const gm_tmpl_piece gm_tplvcuursopmobk_p[];
extern const gm_spec *const gm_tplvcuursopmobk_s[];
extern const gm_tmpl_piece gm_tplveaglbhvc5ls_p[];
extern const gm_spec *const gm_tplvf2bf6hftf4i_s[];
extern const uint32_t gm_tplvflfleui9qjm_i[];
extern const gm_spec *const gm_tplvftnuchaenfj_s[];
extern const gm_tmpl gm_tplvk1pmcp9tr4p;
extern const uint32_t gm_tplvk1pmcp9tr4p_i[];
extern const gm_tmpl_piece gm_tplvk1pmcp9tr4p_p[];
extern const gm_spec *const gm_tplvk1pmcp9tr4p_s[];
extern const gm_tmpl gm_tplvkpfj3qq0aig;
extern const uint32_t gm_tplvkpfj3qq0aig_i[];
extern const gm_tmpl_piece gm_tplvkpfj3qq0aig_p[];
extern const gm_spec *const gm_tplvkpfj3qq0aig_s[];
extern const gm_tmpl_piece gm_tplvkvnlb2orh55_p[];
extern const uint32_t gm_tplvrmulr6rg858_i[];
extern const uint32_t gm_tplvv610d042tt6_i[];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyjmccqdb90f;
extern const gm_type gm_tyo86ks9qkvt;
extern const gm_type gm_tyov4e3mbi0f;
extern const gm_variant gm_tyov4e3mbi0f_v[];
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_i2u6ls4 F;
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l2;
    {
        {
            {
                memmove(((uint8_t *)&F.l1), ((uint8_t *)&F.l0), 56);
            }
            {
                void * t4 = (void *)((uint8_t *)&((struct gm_fr_overlays_6rdbiin *)((&F.h)->up))->l1);
                gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
                gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 24));
                gm_t6j6smj7qoctu t7;
                GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t5);
                GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
                gm_tftb1odc739t5 t8 = gm_f_push_302k12h(t4, &t7);
                gm_l2 = t8;
            }
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcfpg6jsb3cfp6, 549, 36);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 549, 5);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_302k12h F;
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
        gm_b16_8 t9 = gm_f_empty_slice_oq9aajs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5c6uc3s(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5c6uc3s F;
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
const char gm_src0gtuh94b1fce[] = "compiler/cgen/expr.gm";
const char gm_src3ukoldgd7ran[] = "compiler/parse/items.gm";
const char gm_src563df9lf2t92[] = "compiler/interp/frame.gm";
const char gm_src6b5gn9iggr1p[] = "compiler/ratio/ratio.gm";
const char gm_src8lmgjh8bsvgp[] = "compiler/check/exhaust.gm";
const char gm_src8pvk8vd1qnr1[] = "compiler/doc/md_html.gm";
const char gm_src947s096ojljr[] = "lib/std/collections/deque/deque.gm";
const char gm_srceoep1gquqdsq[] = "compiler/fmt/layout.gm";
const char gm_srcfjrpo4i6b0at[] = "compiler/pkg/support.gm";
const char gm_srckg5f2c0p31ur[] = "compiler/sema/generics.gm";
const char gm_srclu7allsekqhf[] = "compiler/check/infer.gm";
const char gm_srcml1jlm4abpm3[] = "compiler/fmt/command.gm";
const char gm_srcmm27gepldtcm[] = "compiler/driver/toolchain.gm";
const char gm_srcmok80ho8hh01[] = "compiler/cgen/func.gm";
const char gm_srcok2brbaepru3[] = "compiler/driver/target.gm";
const char gm_srcp2gphcvgkhg0[] = "compiler/doc/markdown.gm";
const char gm_srcp33bek143l58[] = "compiler/parse/stmt.gm";
const char gm_srcrgc9bkquf6nr[] = "<core>/list.gm";
const char gm_srcuro7l7pd15c2[] = "compiler/cgen/consts.gm";
const gm_spec *const gm_tpl0n7kmejjk8it_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0stjtt2gdn50_s[] = {NULL};
const gm_spec *const gm_tpl16gsv91msflc_s[] = {NULL};
const gm_spec *const gm_tpl1bh0s5ll1m9j_s[] = {NULL};
const gm_spec *const gm_tpl1buo4rn7srlb_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl1os40ii1iecn_s[] = {NULL};
const gm_spec *const gm_tpl1qq6rf1tb5ds_s[] = {NULL};
const gm_spec *const gm_tpl1r51q3hoe535_s[] = {NULL, NULL};
const gm_spec *const gm_tpl24fci6dm83eo_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl265m7jpg5oed_s[] = {NULL};
const gm_spec *const gm_tpl29spsi1atllj_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl2arnv1ofugei_s[] = {NULL};
const gm_spec *const gm_tpl32o7ct8vibbi_s[] = {NULL};
const gm_spec *const gm_tpl335dut97l9dl_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl3bnq2anocte9_s[] = {NULL};
const gm_spec *const gm_tpl3q3m44l3pqqn_s[] = {NULL, NULL};
const gm_spec *const gm_tpl470f2c4pe1su_s[] = {NULL};
const gm_spec *const gm_tpl4e4n6ul1t53g_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl4isjs1bg9fhg_s[] = {NULL};
const gm_spec *const gm_tpl4mli0f9rjihf_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl4phv0vr9as7f_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4tk9leh8nue4_s[] = {NULL, NULL};
const gm_spec *const gm_tpl52cv2e94esne_s[] = {NULL};
const gm_spec *const gm_tpl55mhnmub0ffl_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5677p7i476ao_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl59lqltdr0h0r_s[] = {NULL};
const gm_spec *const gm_tpl5h4mc65ti3jt_s[] = {NULL};
const gm_spec *const gm_tpl5kl598jq5h12_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl5q9oi9aqgs43_s[] = {NULL};
const gm_spec *const gm_tpl68oh02lfq7s5_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6vfpiq020ehi_s[] = {NULL, NULL};
const gm_spec *const gm_tpl7b1ibjg22p4i_s[] = {NULL};
const gm_spec *const gm_tpl7b4l0929mgk1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl7m3t7r6fqpf7_s[] = {NULL};
const gm_spec *const gm_tpl81nh98hgo1rk_s[] = {NULL};
const gm_spec *const gm_tpl82mi3mv6t0is_s[] = {NULL, NULL};
const gm_spec *const gm_tpl896mu4np77il_s[] = {NULL};
const gm_spec *const gm_tpl8geisveh2vug_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl9cnobsjbh0r2_s[] = {NULL};
const gm_spec *const gm_tpl9ljuo8s7738q_s[] = {NULL};
const gm_spec *const gm_tpl9s09hqjpb581_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl9v7lb7n4a602_s[] = {NULL, NULL};
const gm_spec *const gm_tpla15qb3qedeuc_s[] = {NULL};
const gm_spec *const gm_tplacjkb64s2e83_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplaq7jlj8i0s87_s[] = {NULL};
const gm_spec *const gm_tplav8bdvvt8rem_s[] = {NULL};
const gm_spec *const gm_tplb86t2p96msdt_s[] = {NULL, NULL};
const gm_spec *const gm_tplbbopfj3f21eb_s[] = {&(gm_spec){32, 0, 0, 0, 1, 1, 16u, -1}};
const gm_spec *const gm_tplbdrnrpalngql_s[] = {NULL};
const gm_spec *const gm_tplbjh9dr2llnkb_s[] = {NULL, NULL};
const gm_spec *const gm_tplbmcpcu1c6osb_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplbn5t5stpvru2_s[] = {NULL};
const gm_spec *const gm_tplbtqgdimqhkr1_s[] = {NULL, NULL};
const gm_spec *const gm_tplc65rc46hqohp_s[] = {NULL};
const gm_spec *const gm_tplcb1495nujsb7_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplcte0qqjaa0q0_s[] = {NULL};
const gm_spec *const gm_tpldc3nmdc0cit1_s[] = {NULL};
const gm_spec *const gm_tpldhof7u5v5b2c_s[] = {NULL};
const gm_spec *const gm_tpldm28gt81dafq_s[] = {&(gm_spec){32, 0, 0, 0, 1, 2, 2u, -1}};
const gm_spec *const gm_tpldunbbu9dpisn_s[] = {NULL};
const gm_spec *const gm_tple41tg2t64d1i_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tple4upi9cflnq6_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplem02qig8laoi_s[] = {NULL};
const gm_spec *const gm_tpleo7934k1qvrc_s[] = {NULL, NULL};
const gm_spec *const gm_tpleoabj1u2rb54_s[] = {NULL};
const gm_spec *const gm_tplf28d52hmmd2n_s[] = {NULL};
const gm_spec *const gm_tplf8er2klne616_s[] = {NULL};
const gm_spec *const gm_tplf9q60in6m019_s[] = {NULL, NULL};
const gm_spec *const gm_tplfjua3t6jsoe6_s[] = {NULL};
const gm_spec *const gm_tplft0ecs37svcn_s[] = {NULL, NULL};
const gm_spec *const gm_tplg2qlrjk1sunc_s[] = {NULL};
const gm_spec *const gm_tplg45fl0f0qtll_s[] = {NULL};
const gm_spec *const gm_tplgjqa4ejl8op1_s[] = {NULL};
const gm_spec *const gm_tplhojm4dgu514i_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplhr6emralh8q6_s[] = {NULL};
const gm_spec *const gm_tpliqpvgtpo05sn_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplivip1s1762gt_s[] = {&(gm_spec){32, 0, 0, 0, 0, 0, 0u, 2}};
const gm_spec *const gm_tplj16n1o03849r_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplj4gqj9p0fguq_s[] = {NULL, NULL};
const gm_spec *const gm_tplj7j0vuugbh2u_s[] = {NULL, NULL};
const gm_spec *const gm_tplja7aur1oiv70_s[] = {NULL, NULL};
const gm_spec *const gm_tplk3nkr2eh6don_s[] = {NULL};
const gm_spec *const gm_tplkaran1m5lpqm_s[] = {NULL, NULL};
const gm_spec *const gm_tplkfag0fic60vv_s[] = {NULL};
const gm_spec *const gm_tplkinjhvj7vqlj_s[] = {NULL};
const gm_spec *const gm_tplknfbq49i74sp_s[] = {NULL, NULL};
const gm_spec *const gm_tpllbgi055uoiek_s[] = {NULL, NULL};
const gm_spec *const gm_tpllktp07diubcq_s[] = {NULL, NULL};
const gm_spec *const gm_tplls1lesito750_s[] = {NULL};
const gm_spec *const gm_tplm3a458dq976v_s[] = {NULL};
const gm_spec *const gm_tplm3dvc32djhr7_s[] = {NULL};
const gm_spec *const gm_tplmiqlr2bu75gm_s[] = {NULL};
const gm_spec *const gm_tplmo458jpb0n1e_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplmr9p697ovoo7_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpln4t9g9do4qg7_s[] = {NULL, NULL};
const gm_spec *const gm_tpln7ptjfs3pum5_s[] = {NULL};
const gm_spec *const gm_tplnbbj7t6ap7ht_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplngknbnp9i821_s[] = {NULL, NULL};
const gm_spec *const gm_tplnmm43n0jmcod_s[] = {NULL};
const gm_spec *const gm_tploblcdf1g38ca_s[] = {NULL};
const gm_spec *const gm_tplof36cm9vqs8e_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tploj4uaptg6qnk_s[] = {NULL, NULL};
const gm_spec *const gm_tplokvovhsdbec1_s[] = {NULL};
const gm_spec *const gm_tplom6fga2t4kfv_s[] = {NULL};
const gm_spec *const gm_tplop2uhngiv88m_s[] = {NULL};
const gm_spec *const gm_tplovlajhkvntql_s[] = {NULL};
const gm_spec *const gm_tplp3rnngb19vkd_s[] = {NULL};
const gm_spec *const gm_tplpj1vfgivuj13_s[] = {NULL};
const gm_spec *const gm_tplq11qqgpg1tv1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplqfgvjo31nom3_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplqpi0kdavqkt9_s[] = {NULL, NULL};
const gm_spec *const gm_tplr8vkqgn7raqo_s[] = {NULL, NULL};
const gm_spec *const gm_tplrpouqnnj8vo1_s[] = {NULL};
const gm_spec *const gm_tplsjufktgf4o1v_s[] = {NULL};
const gm_spec *const gm_tplt3knh0ehirt7_s[] = {NULL};
const gm_spec *const gm_tplt5ks894658kl_s[] = {NULL};
const gm_spec *const gm_tplt7inuen2vfdh_s[] = {NULL};
const gm_spec *const gm_tplu0n2ra1s30o8_s[] = {NULL, NULL};
const gm_spec *const gm_tplu272587ugnvi_s[] = {NULL, NULL};
const gm_spec *const gm_tplu58b6rd4mq28_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplue01enectvb0_s[] = {NULL};
const gm_spec *const gm_tpluuksm24afvp0_s[] = {NULL};
const gm_spec *const gm_tplv05t19d54qoj_s[] = {NULL, NULL};
const gm_spec *const gm_tplv1i7e9gejv6a_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplvf2bf6hftf4i_s[] = {NULL, NULL};
const gm_spec *const gm_tplvftnuchaenfj_s[] = {NULL};
const gm_tmpl gm_tpl005eoei82778 = {1u, gm_tpl005eoei82778_p, gm_tpl005eoei82778_i, gm_tpl005eoei82778_s};
const gm_tmpl gm_tpl0c7sat2t7j6v = {1u, gm_tpl0c7sat2t7j6v_p, gm_tpl0c7sat2t7j6v_i, gm_tpl0c7sat2t7j6v_s};
const gm_tmpl gm_tpl0e2i60vm7gtr = {1u, gm_tpl0e2i60vm7gtr_p, gm_tpl0e2i60vm7gtr_i, gm_tpl0e2i60vm7gtr_s};
const gm_tmpl gm_tpl0euitr9hdia5 = {1u, gm_tpl0euitr9hdia5_p, gm_tpl0euitr9hdia5_i, gm_tpl0euitr9hdia5_s};
const gm_tmpl gm_tpl0n7kmejjk8it = {2u, gm_tpl0n7kmejjk8it_p, gm_tpl0n7kmejjk8it_i, gm_tpl0n7kmejjk8it_s};
const gm_tmpl gm_tpl0q3rsndpu857 = {1u, gm_tpl0q3rsndpu857_p, gm_tpl0q3rsndpu857_i, gm_tpl0q3rsndpu857_s};
const gm_tmpl gm_tpl0v2pofusugn6 = {1u, gm_tpl0v2pofusugn6_p, gm_tpl0v2pofusugn6_i, gm_tpl0v2pofusugn6_s};
const gm_tmpl gm_tpl16petnrgdcd0 = {1u, gm_tpl16petnrgdcd0_p, gm_tpl16petnrgdcd0_i, gm_tpl16petnrgdcd0_s};
const gm_tmpl gm_tpl1f1njss89nj4 = {2u, gm_tpl1f1njss89nj4_p, gm_tpl1f1njss89nj4_i, gm_tpl1f1njss89nj4_s};
const gm_tmpl gm_tpl1t0vlt7tq67r = {4u, gm_tpl1t0vlt7tq67r_p, gm_tpl1t0vlt7tq67r_i, gm_tpl1t0vlt7tq67r_s};
const gm_tmpl gm_tpl2r84u6vnc58k = {5u, gm_tpl2r84u6vnc58k_p, gm_tpl2r84u6vnc58k_i, gm_tpl2r84u6vnc58k_s};
const gm_tmpl gm_tpl2u97uspakqd9 = {3u, gm_tpl2u97uspakqd9_p, gm_tpl2u97uspakqd9_i, gm_tpl2u97uspakqd9_s};
const gm_tmpl gm_tpl3fu53qt58qjh = {1u, gm_tpl3fu53qt58qjh_p, gm_tpl3fu53qt58qjh_i, gm_tpl3fu53qt58qjh_s};
const gm_tmpl gm_tpl3jlrgt0cfgj3 = {1u, gm_tpl3jlrgt0cfgj3_p, gm_tpl3jlrgt0cfgj3_i, gm_tpl3jlrgt0cfgj3_s};
const gm_tmpl gm_tpl3lqpj3d2egtb = {2u, gm_tpl3lqpj3d2egtb_p, gm_tpl3lqpj3d2egtb_i, gm_tpl3lqpj3d2egtb_s};
const gm_tmpl gm_tpl44cjgoji4tlu = {1u, gm_tpl44cjgoji4tlu_p, gm_tpl44cjgoji4tlu_i, gm_tpl44cjgoji4tlu_s};
const gm_tmpl gm_tpl4c2mfb3cngak = {1u, gm_tpl4c2mfb3cngak_p, gm_tpl4c2mfb3cngak_i, gm_tpl4c2mfb3cngak_s};
const gm_tmpl gm_tpl4ivrecg9ncdu = {2u, gm_tpl4ivrecg9ncdu_p, gm_tpl4ivrecg9ncdu_i, gm_tpl4ivrecg9ncdu_s};
const gm_tmpl gm_tpl4mhnbmvg0r80 = {1u, gm_tpl4mhnbmvg0r80_p, gm_tpl4mhnbmvg0r80_i, gm_tpl4mhnbmvg0r80_s};
const gm_tmpl gm_tpl4npct6bth1c7 = {1u, gm_tpl4npct6bth1c7_p, gm_tpl4npct6bth1c7_i, gm_tpl4npct6bth1c7_s};
const gm_tmpl gm_tpl5536lp5da6eq = {4u, gm_tpl5536lp5da6eq_p, gm_tpl5536lp5da6eq_i, gm_tpl5536lp5da6eq_s};
const gm_tmpl gm_tpl55mhnmub0ffl = {3u, gm_tpl55mhnmub0ffl_p, gm_tpl55mhnmub0ffl_i, gm_tpl55mhnmub0ffl_s};
const gm_tmpl gm_tpl59lqltdr0h0r = {1u, gm_tpl59lqltdr0h0r_p, gm_tpl59lqltdr0h0r_i, gm_tpl59lqltdr0h0r_s};
const gm_tmpl gm_tpl5c6n9e2iidjc = {3u, gm_tpl5c6n9e2iidjc_p, gm_tpl5c6n9e2iidjc_i, gm_tpl5c6n9e2iidjc_s};
const gm_tmpl gm_tpl5hatm6q530pn = {3u, gm_tpl5hatm6q530pn_p, gm_tpl5hatm6q530pn_i, gm_tpl5hatm6q530pn_s};
const gm_tmpl gm_tpl6fas3mugop2g = {1u, gm_tpl6fas3mugop2g_p, gm_tpl6fas3mugop2g_i, gm_tpl6fas3mugop2g_s};
const gm_tmpl gm_tpl6gp425a13qgr = {2u, gm_tpl6gp425a13qgr_p, gm_tpl6gp425a13qgr_i, gm_tpl6gp425a13qgr_s};
const gm_tmpl gm_tpl6pohii1e8pbg = {5u, gm_tpl6pohii1e8pbg_p, gm_tpl6pohii1e8pbg_i, gm_tpl6pohii1e8pbg_s};
const gm_tmpl gm_tpl6s7ufou2uts6 = {1u, gm_tpl6s7ufou2uts6_p, gm_tpl6s7ufou2uts6_i, gm_tpl6s7ufou2uts6_s};
const gm_tmpl gm_tpl726l5jbs5bmh = {1u, gm_tpl726l5jbs5bmh_p, gm_tpl726l5jbs5bmh_i, gm_tpl726l5jbs5bmh_s};
const gm_tmpl gm_tpl737bm31hf71o = {3u, gm_tpl737bm31hf71o_p, gm_tpl737bm31hf71o_i, gm_tpl737bm31hf71o_s};
const gm_tmpl gm_tpl7b4l0929mgk1 = {3u, gm_tpl7b4l0929mgk1_p, gm_tpl7b4l0929mgk1_i, gm_tpl7b4l0929mgk1_s};
const gm_tmpl gm_tpl810t9tkid6i2 = {1u, gm_tpl810t9tkid6i2_p, gm_tpl810t9tkid6i2_i, gm_tpl810t9tkid6i2_s};
const gm_tmpl gm_tpl85p2q6ermgga = {1u, gm_tpl85p2q6ermgga_p, gm_tpl85p2q6ermgga_i, gm_tpl85p2q6ermgga_s};
const gm_tmpl gm_tpl98as72l6ukas = {2u, gm_tpl98as72l6ukas_p, gm_tpl98as72l6ukas_i, gm_tpl98as72l6ukas_s};
const gm_tmpl gm_tpl9900stiqclaj = {1u, gm_tpl9900stiqclaj_p, gm_tpl9900stiqclaj_i, gm_tpl9900stiqclaj_s};
const gm_tmpl gm_tpl9qc0voknsp7g = {1u, gm_tpl9qc0voknsp7g_p, gm_tpl9qc0voknsp7g_i, gm_tpl9qc0voknsp7g_s};
const gm_tmpl gm_tpl9vu8tqcv1d9s = {2u, gm_tpl9vu8tqcv1d9s_p, gm_tpl9vu8tqcv1d9s_i, gm_tpl9vu8tqcv1d9s_s};
const gm_tmpl gm_tpla5rqmr49dnnk = {4u, gm_tpla5rqmr49dnnk_p, gm_tpla5rqmr49dnnk_i, gm_tpla5rqmr49dnnk_s};
const gm_tmpl gm_tpla6b7o385jo3o = {1u, gm_tpla6b7o385jo3o_p, gm_tpla6b7o385jo3o_i, gm_tpla6b7o385jo3o_s};
const gm_tmpl gm_tpla6rojfclgc54 = {1u, gm_tpla6rojfclgc54_p, gm_tpla6rojfclgc54_i, gm_tpla6rojfclgc54_s};
const gm_tmpl gm_tpla8nvlakostko = {1u, gm_tpla8nvlakostko_p, gm_tpla8nvlakostko_i, gm_tpla8nvlakostko_s};
const gm_tmpl gm_tplacjeuo4at0jj = {2u, gm_tplacjeuo4at0jj_p, gm_tplacjeuo4at0jj_i, gm_tplacjeuo4at0jj_s};
const gm_tmpl gm_tplae6k9mv0s1dm = {2u, gm_tplae6k9mv0s1dm_p, gm_tplae6k9mv0s1dm_i, gm_tplae6k9mv0s1dm_s};
const gm_tmpl gm_tplb2gsdj8iioqj = {2u, gm_tplb2gsdj8iioqj_p, gm_tplb2gsdj8iioqj_i, gm_tplb2gsdj8iioqj_s};
const gm_tmpl gm_tplbeuo81m3df3c = {1u, gm_tplbeuo81m3df3c_p, gm_tplbeuo81m3df3c_i, gm_tplbeuo81m3df3c_s};
const gm_tmpl gm_tplbmf4neusqbo5 = {3u, gm_tplbmf4neusqbo5_p, gm_tplbmf4neusqbo5_i, gm_tplbmf4neusqbo5_s};
const gm_tmpl gm_tplc5cre0uqposd = {2u, gm_tplc5cre0uqposd_p, gm_tplc5cre0uqposd_i, gm_tplc5cre0uqposd_s};
const gm_tmpl gm_tplcilp3mvhfo4n = {1u, gm_tplcilp3mvhfo4n_p, gm_tplcilp3mvhfo4n_i, gm_tplcilp3mvhfo4n_s};
const gm_tmpl gm_tplck4jpb6nd99p = {1u, gm_tplck4jpb6nd99p_p, gm_tplck4jpb6nd99p_i, gm_tplck4jpb6nd99p_s};
const gm_tmpl gm_tpld1ficc6ikf1e = {1u, gm_tpld1ficc6ikf1e_p, gm_tpld1ficc6ikf1e_i, gm_tpld1ficc6ikf1e_s};
const gm_tmpl gm_tpldp3bk13rlc44 = {1u, gm_tpldp3bk13rlc44_p, gm_tpldp3bk13rlc44_i, gm_tpldp3bk13rlc44_s};
const gm_tmpl gm_tpldvh3e1jcen1u = {2u, gm_tpldvh3e1jcen1u_p, gm_tpldvh3e1jcen1u_i, gm_tpldvh3e1jcen1u_s};
const gm_tmpl gm_tple7i0r0sg6sjs = {1u, gm_tple7i0r0sg6sjs_p, gm_tple7i0r0sg6sjs_i, gm_tple7i0r0sg6sjs_s};
const gm_tmpl gm_tpled61een8fpfm = {1u, gm_tpled61een8fpfm_p, gm_tpled61een8fpfm_i, gm_tpled61een8fpfm_s};
const gm_tmpl gm_tplf1r4lr6ofdql = {2u, gm_tplf1r4lr6ofdql_p, gm_tplf1r4lr6ofdql_i, gm_tplf1r4lr6ofdql_s};
const gm_tmpl gm_tplfkugp8hqqhvu = {3u, gm_tplfkugp8hqqhvu_p, gm_tplfkugp8hqqhvu_i, gm_tplfkugp8hqqhvu_s};
const gm_tmpl gm_tplg0rms0t7302m = {1u, gm_tplg0rms0t7302m_p, gm_tplg0rms0t7302m_i, gm_tplg0rms0t7302m_s};
const gm_tmpl gm_tplg45fl0f0qtll = {1u, gm_tplg45fl0f0qtll_p, gm_tplg45fl0f0qtll_i, gm_tplg45fl0f0qtll_s};
const gm_tmpl gm_tplg45jfs21ci2c = {1u, gm_tplg45jfs21ci2c_p, gm_tplg45jfs21ci2c_i, gm_tplg45jfs21ci2c_s};
const gm_tmpl gm_tplgemvjdfjljaa = {2u, gm_tplgemvjdfjljaa_p, gm_tplgemvjdfjljaa_i, gm_tplgemvjdfjljaa_s};
const gm_tmpl gm_tplgqohfqetaph7 = {1u, gm_tplgqohfqetaph7_p, gm_tplgqohfqetaph7_i, gm_tplgqohfqetaph7_s};
const gm_tmpl gm_tplh2em7r1dmffm = {1u, gm_tplh2em7r1dmffm_p, gm_tplh2em7r1dmffm_i, gm_tplh2em7r1dmffm_s};
const gm_tmpl gm_tplh78gsuiavvas = {1u, gm_tplh78gsuiavvas_p, gm_tplh78gsuiavvas_i, gm_tplh78gsuiavvas_s};
const gm_tmpl gm_tplhatoh83mi4is = {1u, gm_tplhatoh83mi4is_p, gm_tplhatoh83mi4is_i, gm_tplhatoh83mi4is_s};
const gm_tmpl gm_tplhdb29uo8rm6j = {2u, gm_tplhdb29uo8rm6j_p, gm_tplhdb29uo8rm6j_i, gm_tplhdb29uo8rm6j_s};
const gm_tmpl gm_tplhiqpct20jd67 = {1u, gm_tplhiqpct20jd67_p, gm_tplhiqpct20jd67_i, gm_tplhiqpct20jd67_s};
const gm_tmpl gm_tplhl4q6p2b5f92 = {1u, gm_tplhl4q6p2b5f92_p, gm_tplhl4q6p2b5f92_i, gm_tplhl4q6p2b5f92_s};
const gm_tmpl gm_tplhr6emralh8q6 = {1u, gm_tplhr6emralh8q6_p, gm_tplhr6emralh8q6_i, gm_tplhr6emralh8q6_s};
const gm_tmpl gm_tpliq8p2spcvq06 = {2u, gm_tpliq8p2spcvq06_p, gm_tpliq8p2spcvq06_i, gm_tpliq8p2spcvq06_s};
const gm_tmpl gm_tpliqi7pna59a9o = {1u, gm_tpliqi7pna59a9o_p, gm_tpliqi7pna59a9o_i, gm_tpliqi7pna59a9o_s};
const gm_tmpl gm_tpliuif0nifmui1 = {2u, gm_tpliuif0nifmui1_p, gm_tpliuif0nifmui1_i, gm_tpliuif0nifmui1_s};
const gm_tmpl gm_tpljd9codq72bc9 = {1u, gm_tpljd9codq72bc9_p, gm_tpljd9codq72bc9_i, gm_tpljd9codq72bc9_s};
const gm_tmpl gm_tplkd4irlmplk3e = {2u, gm_tplkd4irlmplk3e_p, gm_tplkd4irlmplk3e_i, gm_tplkd4irlmplk3e_s};
const gm_tmpl gm_tplkodpfg3ik1kh = {1u, gm_tplkodpfg3ik1kh_p, gm_tplkodpfg3ik1kh_i, gm_tplkodpfg3ik1kh_s};
const gm_tmpl gm_tplksrq9kcn68vt = {1u, gm_tplksrq9kcn68vt_p, gm_tplksrq9kcn68vt_i, gm_tplksrq9kcn68vt_s};
const gm_tmpl gm_tpllbu1ke0v3v7a = {2u, gm_tpllbu1ke0v3v7a_p, gm_tpllbu1ke0v3v7a_i, gm_tpllbu1ke0v3v7a_s};
const gm_tmpl gm_tpllf7nufk7lvkm = {2u, gm_tpllf7nufk7lvkm_p, gm_tpllf7nufk7lvkm_i, gm_tpllf7nufk7lvkm_s};
const gm_tmpl gm_tplli6rlvq5vnvb = {3u, gm_tplli6rlvq5vnvb_p, gm_tplli6rlvq5vnvb_i, gm_tplli6rlvq5vnvb_s};
const gm_tmpl gm_tplm2m182mt5ljj = {1u, gm_tplm2m182mt5ljj_p, gm_tplm2m182mt5ljj_i, gm_tplm2m182mt5ljj_s};
const gm_tmpl gm_tplmnlel5aohei6 = {1u, gm_tplmnlel5aohei6_p, gm_tplmnlel5aohei6_i, gm_tplmnlel5aohei6_s};
const gm_tmpl gm_tplmqflu0tp0eas = {1u, gm_tplmqflu0tp0eas_p, gm_tplmqflu0tp0eas_i, gm_tplmqflu0tp0eas_s};
const gm_tmpl gm_tpln2j7s58m8uan = {5u, gm_tpln2j7s58m8uan_p, gm_tpln2j7s58m8uan_i, gm_tpln2j7s58m8uan_s};
const gm_tmpl gm_tpln38mjgenps81 = {2u, gm_tpln38mjgenps81_p, gm_tpln38mjgenps81_i, gm_tpln38mjgenps81_s};
const gm_tmpl gm_tplngknbnp9i821 = {2u, gm_tplngknbnp9i821_p, gm_tplngknbnp9i821_i, gm_tplngknbnp9i821_s};
const gm_tmpl gm_tplnrt3ito76b2v = {1u, gm_tplnrt3ito76b2v_p, gm_tplnrt3ito76b2v_i, gm_tplnrt3ito76b2v_s};
const gm_tmpl gm_tplof36cm9vqs8e = {8u, gm_tplof36cm9vqs8e_p, gm_tplof36cm9vqs8e_i, gm_tplof36cm9vqs8e_s};
const gm_tmpl gm_tplok9pv33vcsg0 = {1u, gm_tplok9pv33vcsg0_p, gm_tplok9pv33vcsg0_i, gm_tplok9pv33vcsg0_s};
const gm_tmpl gm_tplokvovhsdbec1 = {1u, gm_tplokvovhsdbec1_p, gm_tplokvovhsdbec1_i, gm_tplokvovhsdbec1_s};
const gm_tmpl gm_tplolonhuuitsj2 = {1u, gm_tplolonhuuitsj2_p, gm_tplolonhuuitsj2_i, gm_tplolonhuuitsj2_s};
const gm_tmpl gm_tplooqfb0umhrq0 = {4u, gm_tplooqfb0umhrq0_p, gm_tplooqfb0umhrq0_i, gm_tplooqfb0umhrq0_s};
const gm_tmpl gm_tplp3l2s17nqpoi = {1u, gm_tplp3l2s17nqpoi_p, gm_tplp3l2s17nqpoi_i, gm_tplp3l2s17nqpoi_s};
const gm_tmpl gm_tplpf7ckn15kjth = {1u, gm_tplpf7ckn15kjth_p, gm_tplpf7ckn15kjth_i, gm_tplpf7ckn15kjth_s};
const gm_tmpl gm_tplq0gioeoiukeh = {2u, gm_tplq0gioeoiukeh_p, gm_tplq0gioeoiukeh_i, gm_tplq0gioeoiukeh_s};
const gm_tmpl gm_tplq0mvos8lisnf = {1u, gm_tplq0mvos8lisnf_p, gm_tplq0mvos8lisnf_i, gm_tplq0mvos8lisnf_s};
const gm_tmpl gm_tplq75gqr5tvlmd = {2u, gm_tplq75gqr5tvlmd_p, gm_tplq75gqr5tvlmd_i, gm_tplq75gqr5tvlmd_s};
const gm_tmpl gm_tplqjo10upi8bs6 = {1u, gm_tplqjo10upi8bs6_p, gm_tplqjo10upi8bs6_i, gm_tplqjo10upi8bs6_s};
const gm_tmpl gm_tplqrbhvccemc9k = {1u, gm_tplqrbhvccemc9k_p, gm_tplqrbhvccemc9k_i, gm_tplqrbhvccemc9k_s};
const gm_tmpl gm_tplrbicm1oan2em = {1u, gm_tplrbicm1oan2em_p, gm_tplrbicm1oan2em_i, gm_tplrbicm1oan2em_s};
const gm_tmpl gm_tplrd5hip684e7e = {1u, gm_tplrd5hip684e7e_p, gm_tplrd5hip684e7e_i, gm_tplrd5hip684e7e_s};
const gm_tmpl gm_tplrffp652tkgo2 = {4u, gm_tplrffp652tkgo2_p, gm_tplrffp652tkgo2_i, gm_tplrffp652tkgo2_s};
const gm_tmpl gm_tplrtgv8em132rl = {2u, gm_tplrtgv8em132rl_p, gm_tplrtgv8em132rl_i, gm_tplrtgv8em132rl_s};
const gm_tmpl gm_tpls5bjfpqmr7e0 = {1u, gm_tpls5bjfpqmr7e0_p, gm_tpls5bjfpqmr7e0_i, gm_tpls5bjfpqmr7e0_s};
const gm_tmpl gm_tpls85ptecv5rkj = {1u, gm_tpls85ptecv5rkj_p, gm_tpls85ptecv5rkj_i, gm_tpls85ptecv5rkj_s};
const gm_tmpl gm_tplsmpaecrda5mc = {3u, gm_tplsmpaecrda5mc_p, gm_tplsmpaecrda5mc_i, gm_tplsmpaecrda5mc_s};
const gm_tmpl gm_tplsns868vihk0u = {4u, gm_tplsns868vihk0u_p, gm_tplsns868vihk0u_i, gm_tplsns868vihk0u_s};
const gm_tmpl gm_tplsrr2cfplv9ff = {1u, gm_tplsrr2cfplv9ff_p, gm_tplsrr2cfplv9ff_i, gm_tplsrr2cfplv9ff_s};
const gm_tmpl gm_tplt320ru9bfpdd = {1u, gm_tplt320ru9bfpdd_p, gm_tplt320ru9bfpdd_i, gm_tplt320ru9bfpdd_s};
const gm_tmpl gm_tpltktipp7m43tk = {3u, gm_tpltktipp7m43tk_p, gm_tpltktipp7m43tk_i, gm_tpltktipp7m43tk_s};
const gm_tmpl gm_tpltue438lnncbq = {1u, gm_tpltue438lnncbq_p, gm_tpltue438lnncbq_i, gm_tpltue438lnncbq_s};
const gm_tmpl gm_tplu7oi9kuo0adg = {1u, gm_tplu7oi9kuo0adg_p, gm_tplu7oi9kuo0adg_i, gm_tplu7oi9kuo0adg_s};
const gm_tmpl gm_tplua69gir871ue = {1u, gm_tplua69gir871ue_p, gm_tplua69gir871ue_i, gm_tplua69gir871ue_s};
const gm_tmpl gm_tplueje3nombrfc = {4u, gm_tplueje3nombrfc_p, gm_tplueje3nombrfc_i, gm_tplueje3nombrfc_s};
const gm_tmpl gm_tpluttgpct24aib = {1u, gm_tpluttgpct24aib_p, gm_tpluttgpct24aib_i, gm_tpluttgpct24aib_s};
const gm_tmpl gm_tpluuts50bm11md = {1u, gm_tpluuts50bm11md_p, gm_tpluuts50bm11md_i, gm_tpluuts50bm11md_s};
const gm_tmpl gm_tplv6dbvs9bjnes = {2u, gm_tplv6dbvs9bjnes_p, gm_tplv6dbvs9bjnes_i, gm_tplv6dbvs9bjnes_s};
const gm_tmpl gm_tplvbcfr0k6nmhv = {2u, gm_tplvbcfr0k6nmhv_p, gm_tplvbcfr0k6nmhv_i, gm_tplvbcfr0k6nmhv_s};
const gm_tmpl gm_tplvcuursopmobk = {1u, gm_tplvcuursopmobk_p, gm_tplvcuursopmobk_i, gm_tplvcuursopmobk_s};
const gm_tmpl gm_tplvk1pmcp9tr4p = {1u, gm_tplvk1pmcp9tr4p_p, gm_tplvk1pmcp9tr4p_i, gm_tplvk1pmcp9tr4p_s};
const gm_tmpl gm_tplvkpfj3qq0aig = {1u, gm_tplvkpfj3qq0aig_p, gm_tplvkpfj3qq0aig_i, gm_tplvkpfj3qq0aig_s};
const gm_tmpl_piece gm_tpl00b7kgp37ng7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_st3ja0nqi37bv, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl05osb04qe3ad_p[] = {{gm_s7hkl5mqa15gm, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0cgce8migcnp_p[] = {{gm_s0j5c49gnifu8, 17}, {gm_sld9l7nonn4o9, 16}};
const gm_tmpl_piece gm_tpl0e782j7cr191_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s9bggu8oepdvs, 18}, {gm_s3e9133b3pasn, 2}, {gm_s0t5omnh472sf, 20}};
const gm_tmpl_piece gm_tpl0sb47rg6hcij_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s9b81q4kouna6, 10}, {gm_s8pvtaofn65p9, 28}, {gm_sc24a25pi012e, 7}, {gm_snukl6jqep6jf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0tr163o4bba1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s33h2lce46bnm, 8}, {gm_slvu7caka1qb7, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl13591hp4g6qh_p[] = {{gm_snhkhrbd8tjtf, 44}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl14n6ndgi62j1_p[] = {{gm_snps96dj7ecq0, 16}, {gm_s8abg3vemofr6, 3}, {gm_s4v8j7s5sr5c1, 65}};
const gm_tmpl_piece gm_tpl1u8koiam5nmo_p[] = {{gm_s88lqrfvhc8i3, 45}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl265m7jpg5oed_p[] = {{gm_sfi2ha42r1nr2, 1}, {gm_stabrbc330nqe, 17}};
const gm_tmpl_piece gm_tpl2arnv1ofugei_p[] = {{gm_s04a35h53le03, 42}, {gm_stapduvfaru1r, 67}};
const gm_tmpl_piece gm_tpl2g2o9ile1980_p[] = {{gm_shmpls8n0g8h9, 3}, {gm_shde4bsckk9mt, 3}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl2re1r0fre48m_p[] = {{gm_sdt2alvv2h6jh, 47}, {gm_s72l6okdj2d3g, 94}};
const gm_tmpl_piece gm_tpl3sv6aqbksrr5_p[] = {{gm_s73vhpsrl3o41, 30}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl3tcah2c39gjk_p[] = {{gm_sthtklncdroln, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4561ar79q0c0_p[] = {{gm_s3ndmfc1bk22f, 22}, {gm_snvn4jaklkik3, 64}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl4nfbp2ikscbk_p[] = {{gm_smd171q6j6al8, 19}, {gm_s5m9qimbv7e8k, 2}};
const gm_tmpl_piece gm_tpl5uqj3jbiq5f0_p[] = {{gm_sg5gdnrith2i6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6bjoajp2fh2n_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_ss34ilcmun9p0, 18}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sb281auin9fds, 19}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sq12h87ngj5nl, 8}};
const gm_tmpl_piece gm_tpl6lldue1mm65c_p[] = {{gm_soo0b43e4t7sn, 11}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl6r9reg6pnruq_p[] = {{gm_squv1ktk9abst, 40}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6rgl0akrj87c_p[] = {{gm_sqk8phuvunvgk, 55}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl6skdtj6qlkmd_p[] = {{gm_s1tqe3mtj6vb1, 7}, {gm_scrq475mutg06, 3}};
const gm_tmpl_piece gm_tpl6tnfk0d9dl8e_p[] = {{gm_sihb3eljrjar6, 15}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7b2kb2ainpff_p[] = {{gm_sm55k23067j17, 15}, {gm_s24s9g8r9gc0v, 6}, {gm_s23t712hkqlmp, 4}, {gm_s3brf0f9sa1j7, 25}, {gm_sub7l8h1sjc33, 11}, {gm_s39mljh669qgl, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl81nh98hgo1rk_p[] = {{gm_secrjke11f3ph, 12}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpl82nt5tav1er7_p[] = {{gm_sbe2lidu87fed, 60}, {gm_sb5b70djeqhg8, 15}};
const gm_tmpl_piece gm_tpl85lm1gq83hlb_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sgks95jf6fs73, 20}, {gm_smooaouqmm8dj, 1}, {gm_s50uau4el7or4, 16}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl8nmc259qeh4g_p[] = {{gm_srvl2rg1hpk9a, 15}, {gm_s0n03nrf296lf, 32}, {gm_s4rugtmn8jarn, 56}};
const gm_tmpl_piece gm_tpl8nqn0hncnfd4_p[] = {{gm_s02aodrl2fpra, 11}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl8pvdc7qbgge6_p[] = {{gm_snk5sdi9p8rv7, 4}, {gm_sm5be4659ulci, 1}};
const gm_tmpl_piece gm_tpl9225uv77leci_p[] = {{gm_s1bpuqd44lur7, 6}, {gm_su2ndlglvt2c0, 10}};
const gm_tmpl_piece gm_tpl9bim66fl3b37_p[] = {{gm_sono5n5lrjd7h, 9}, {gm_sao4mumvadoct, 3}, {gm_sr8m5cvuum3fb, 4}};
const gm_tmpl_piece gm_tpl9p5406jtlms7_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sgchj7ubg6umm, 19}};
const gm_tmpl_piece gm_tpla843kfhqtvit_p[] = {{gm_sf4ahu9mfer7v, 4}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplaqje7ooa5hcl_p[] = {{gm_s5c0ruf0vg8mu, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplau4cbc1piuvn_p[] = {{gm_s33fm62gcdbhu, 10}, {gm_sisp52otedii5, 22}, {gm_sjifgfpiu6k52, 5}, {gm_sejtof4qcrkoq, 7}};
const gm_tmpl_piece gm_tplbmcpcu1c6osb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sjfdnh735o7f8, 4}, {gm_srv4dcfccuset, 2}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplbo9rmalpf2vj_p[] = {{gm_s7h5sk98p4bgn, 16}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sjcm18p35o9rq, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplbqqnfekph537_p[] = {{gm_stt0il17c6uj9, 16}, {gm_s4fi69869ooj8, 27}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcein5d5pq2cs_p[] = {{gm_sodgjk6pvm3b4, 20}, {gm_s5el4r6boe881, 13}, {gm_smdjnv4d2cfrh, 30}};
const gm_tmpl_piece gm_tpldff5gvnhne6f_p[] = {{gm_s0qbjm15aot6s, 10}, {gm_sg5gdnrith2i6, 2}, {gm_shrelr880pj0m, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldknbfev12jrb_p[] = {{gm_sda1bf43v6985, 9}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_sqjlosotosjfp, 17}};
const gm_tmpl_piece gm_tpldqrk6ll2jb23_p[] = {{gm_svl4l2v56ma30, 7}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpldu1003b7br77_p[] = {{gm_semhbv8seph85, 20}, {gm_sspbbn4skfqau, 7}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplec2d6tt24j0q_p[] = {{gm_sio6bogdhis0p, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplegelorakvcn7_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sa71447djt33j, 48}};
const gm_tmpl_piece gm_tplenatqap1hjoj_p[] = {{gm_s3op5s1agh0ju, 34}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplf3c2iulsht2e_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}};
const gm_tmpl_piece gm_tplf6770ieb5fa8_p[] = {{gm_sppeomacjvkpm, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplfft5kohcphn8_p[] = {{gm_som0q53jqvjh3, 4}, {gm_ssn1v6q1cq4so, 2}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tplfg7j0v7js1f7_p[] = {{gm_spg4d1df6jnji, 6}, {gm_sqr1rjjq3dpuh, 16}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfo19v5itil8t_p[] = {{gm_ss5q3rrcb010f, 26}, {gm_st5nnur2j246q, 35}, {gm_s9236sqnmc8uc, 8}};
const gm_tmpl_piece gm_tplfsmasr3tgalo_p[] = {{gm_sokpg5qd8r9va, 19}, {gm_sf0p9o7g7fr6l, 6}, {gm_sg398rgmck67b, 3}};
const gm_tmpl_piece gm_tplft0ecs37svcn_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s7mie2m30h1s9, 27}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplg24lfmv0pffq_p[] = {{gm_snaenr23tbgmb, 22}, {gm_s3347hvco05di, 5}, {gm_s4l2s2smm9apo, 20}};
const gm_tmpl_piece gm_tplggs57b0p2or4_p[] = {{gm_st62vtcrpam9i, 16}, {gm_s4hp8mhves75a, 40}};
const gm_tmpl_piece gm_tplgja2nber62dc_p[] = {{gm_smooaouqmm8dj, 1}, {gm_sstr4ffsnqfee, 5}};
const gm_tmpl_piece gm_tplgjqa4ejl8op1_p[] = {{gm_som0q53jqvjh3, 4}, {gm_sbupitjaj376b, 33}};
const gm_tmpl_piece gm_tplgnp9u7qeb3h6_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_shh36vc8p1t4e, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplgvu725182gad_p[] = {{gm_st87qih3spnnu, 14}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplhgdgs8lv4h7d_p[] = {{gm_s898uefj5c9k6, 27}, {gm_s8abg3vemofr6, 3}, {gm_sq98g0itpb0cn, 18}, {gm_snvncjka7ph6m, 6}, {gm_sc12loraoaq3m, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhmairn7oibac_p[] = {{gm_smooaouqmm8dj, 1}, {gm_ssbcl1ouchrlk, 38}, {gm_s0m0qaokk51g0, 3}, {gm_sr26j6lp0gfqo, 10}, {gm_smooaouqmm8dj, 1}, {gm_si4l87dl4tpkq, 7}, {gm_ssk4a01soguqj, 6}, {gm_st9gkvtvi92qe, 16}, {gm_shv6kkd8qg13e, 37}, {gm_s0m0qaokk51g0, 3}, {gm_sf22i79i4hbq1, 6}, {gm_smooaouqmm8dj, 1}, {gm_si4l87dl4tpkq, 7}, {gm_ssk4a01soguqj, 6}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplhrko1gs9n6tk_p[] = {{gm_s4sak9mcv04d9, 24}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tpli824iv2hf4o6_p[] = {{gm_sam1nkbp8dhs9, 15}, {gm_sjltai3b82h79, 3}, {gm_s7a0708p0cdgv, 37}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplibksf6f0e00q_p[] = {{gm_so3ukp67pds22, 189}, {gm_sjf1r2hmheh87, 28}};
const gm_tmpl_piece gm_tpliig5tr0gjn5a_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_spj9rva8kb5ar, 2}, {gm_s77fi68iu5q6n, 4}};
const gm_tmpl_piece gm_tplj972sj9sp61d_p[] = {{gm_sdbekdpro29kh, 38}, {gm_ss0g40l133d39, 12}, {gm_smooaouqmm8dj, 1}, {gm_saveva62qedei, 7}, {gm_sg5gdnrith2i6, 2}, {gm_s0adesbq24e9u, 5}, {gm_srvi01p0as8h9, 14}};
const gm_tmpl_piece gm_tpljd9codq72bc9_p[] = {{gm_s4vqnsfknimfg, 27}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljhar6jmbtsst_p[] = {{gm_sqs6r7f5gl2vu, 25}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljueetjft1ril_p[] = {{gm_spkrf9e7cr6h4, 6}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpll6jntfkrhtt4_p[] = {{gm_som0q53jqvjh3, 4}, {gm_scedkbh0j6d78, 5}, {gm_s2kl2d59c04nf, 18}};
const gm_tmpl_piece gm_tpllaurt6qemv8q_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s11aakm4d1c2s, 18}};
const gm_tmpl_piece gm_tplld08lp5kl07i_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s4qg6lddtahb8, 3}};
const gm_tmpl_piece gm_tpllgrg03l2v74l_p[] = {{gm_sivdpq4bdf3h5, 16}, {gm_srsbjtfrhs8q1, 15}, {gm_smooaouqmm8dj, 1}, {gm_s7teaveu6apab, 3}};
const gm_tmpl_piece gm_tpllqboe50ngs7g_p[] = {{gm_sh4b8t54h2jna, 48}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplm16s64mnihe4_p[] = {{gm_s66fbu3uhl900, 19}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplm3a458dq976v_p[] = {{gm_saturamfhig5d, 44}, {gm_s989kd388iles, 4}};
const gm_tmpl_piece gm_tplm9hj31tk9ip4_p[] = {{gm_sbvdm2nk31pvh, 8}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplm9t4pej720h2_p[] = {{gm_spmv5l6cklh5d, 17}, {gm_sfcgvtliokb7a, 27}, {gm_sq64eqmcuoej6, 14}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplmcmsjqrp0d64_p[] = {{gm_sb658vbovt6jg, 36}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplmn77qas0g7s4_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sngckqdbegtlc, 25}};
const gm_tmpl_piece gm_tplmqflu0tp0eas_p[] = {{gm_s4h2qpm1qm9a0, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplmsd0sfgf2v66_p[] = {{gm_s03c24qg7i06q, 12}, {gm_skd5c72k4pcr6, 22}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplmv1ukn138isf_p[] = {{gm_sda1bf43v6985, 9}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_snvodad5t43r7, 10}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tpln1554he46gkc_p[] = {{gm_s823123ng02vo, 43}, {gm_sl0n65k3lfps6, 17}};
const gm_tmpl_piece gm_tpln5ioe1q518f0_p[] = {{gm_s0m0qaokk51g0, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplnmm43n0jmcod_p[] = {{gm_scc5n7285iivu, 12}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplnrfprs01gfmj_p[] = {{gm_s3jl3afv8nlvv, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplo08f1kg3k97n_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sgip9ekbho6mc, 10}, {gm_s2g9gpcumahrc, 23}, {gm_spf5krhuq011v, 10}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplo6lbijqleti1_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sq6kdq5587s3n, 10}, {gm_s3iij9a939gcv, 14}, {gm_srq5utruoa2v3, 27}, {gm_skgulapvvrhkp, 35}, {gm_s7agona8glbka, 12}, {gm_so05t2goj6895, 23}, {gm_si6sr5gqkcui7, 5}};
const gm_tmpl_piece gm_tplotost4gd8bha_p[] = {{gm_su03its4kclfl, 9}, {gm_stdh0kbfgj33k, 8}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplpav1vs8t9nu9_p[] = {{gm_sidlbibo929o4, 11}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpd6kdal33co2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sg26if17vtome, 22}, {gm_shdh4dd2ejebj, 157}};
const gm_tmpl_piece gm_tplpkq4i4j83tmk_p[] = {{gm_s3o37fp7c0sqt, 14}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplqm5shuhajt4a_p[] = {{gm_smbbaf3n1gml2, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplqn6e8rqmlvpo_p[] = {{gm_s1l7s35k2bj17, 7}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplr7vr7aubja28_p[] = {{gm_sbogo0q916fbh, 18}, {gm_sspmmsm16lqb6, 28}};
const gm_tmpl_piece gm_tplsdokr8v76bhc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sesqjijoh7km4, 8}};
const gm_tmpl_piece gm_tplsf6r3t44km8l_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svt8aldhoetro, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsleqso6g0tjq_p[] = {{gm_ssfg877iegnma, 3}, {gm_s97nfli622uqg, 4}};
const gm_tmpl_piece gm_tplso01g2paq6o6_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s9r71kmnlqpv8, 16}, {gm_s72tasmt4pg49, 25}};
const gm_tmpl_piece gm_tplu65cpe6nhlbb_p[] = {{gm_smk08v391jpq1, 22}, {gm_s52od60hi0cpi, 20}};
const gm_tmpl_piece gm_tplueje3nombrfc_p[] = {{gm_sufoqrq4onvgh, 15}, {gm_sgbcvt6rqctvd, 6}, {gm_sjcm18p35o9rq, 3}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplukjghf5ng2te_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sp7cdd0n91vli, 7}, {gm_s08ekjvgo9fro, 100}};
const gm_tmpl_piece gm_tplv23k7silrvnp_p[] = {{gm_so81pe3qfh0hc, 14}, {gm_s53c18pcajigs, 50}};
const gm_tmpl_piece gm_tplv8ckuk7p1s8m_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s9krd4pni7gv1, 5}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplveaglbhvc5ls_p[] = {{gm_seh12nehvgmhj, 11}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplvkvnlb2orh55_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s39mljh669qgl, 1}, {gm_s39mljh669qgl, 1}, {gm_slgmqgkioa4u6, 2}};
const gm_type gm_ty0iqiubfm7e = {.size = 16, .align = 8, .kind = GM_K_SLICE, .elem = &gm_tyjmccqdb90f};
const gm_type gm_ty0iqiubfm7e;
const gm_type gm_tybihgc69jv3 = {.size = 16, .align = 8, .kind = GM_K_SLICE, .elem = &gm_tyjmccqdb90f};
const gm_type gm_tybihgc69jv3;
const gm_type gm_tyov4e3mbi0f = {.size = 1, .align = 1, .name = "RmwOp", .kind = GM_K_UNION, .nvariants = 7, .variants = gm_tyov4e3mbi0f_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyov4e3mbi0f;
const uint32_t gm_tpl00nmcu166crh_i[] = {0u};
const uint32_t gm_tpl00rv7vuig9ur_i[] = {0u};
const uint32_t gm_tpl04hr2npsr626_i[] = {0u};
const uint32_t gm_tpl0eln6evo1vf9_i[] = {0u};
const uint32_t gm_tpl0fhs0t9qdf1o_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0tljhf3n7k2p_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0uott3s3afvp_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl17t0pmu1kh1u_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl1lvejpvpq0qh_i[] = {0u, 1u};
const uint32_t gm_tpl1u24a06iag0n_i[] = {0u};
const uint32_t gm_tpl1vbgcviqausd_i[] = {0u, 1u};
const uint32_t gm_tpl2a7pmi7aq67m_i[] = {0u};
const uint32_t gm_tpl2dia2gbnfh9l_i[] = {0u};
const uint32_t gm_tpl2fcbm03ka11j_i[] = {0u, 1u};
const uint32_t gm_tpl3052vn0bjbcl_i[] = {0u, 1u};
const uint32_t gm_tpl44b660pgg1c8_i[] = {0u, 1u};
const uint32_t gm_tpl4561ar79q0c0_i[] = {0u, 1u};
const uint32_t gm_tpl4o9g8c43tino_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl570sljg0ivt5_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl5a8og2hcltt3_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl5c6n9e2iidjc_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl5cgoa6ddohcv_i[] = {0u};
const uint32_t gm_tpl5d3ksa1ntdkn_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl65jqebqot15g_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl69qlgb4nvkqp_i[] = {0u, 1u};
const uint32_t gm_tpl6mra39d4fo2s_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl6skdtj6qlkmd_i[] = {0u};
const uint32_t gm_tpl6vfpiq020ehi_i[] = {0u, 1u};
const uint32_t gm_tpl737bm31hf71o_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl77gpruiccsl2_i[] = {0u};
const uint32_t gm_tpl7b1ibjg22p4i_i[] = {0u};
const uint32_t gm_tpl7cjap0b34jjm_i[] = {0u};
const uint32_t gm_tpl7ut0r685hcaa_i[] = {0u};
const uint32_t gm_tpl81nunf5qvo6u_i[] = {0u};
const uint32_t gm_tpl81uel28fst2a_i[] = {0u, 1u};
const uint32_t gm_tpl84co505h34ud_i[] = {0u};
const uint32_t gm_tpl8geisveh2vug_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
const uint32_t gm_tpl8gj5o7rmo2fl_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl8ro9atn5p92f_i[] = {0u, 1u};
const uint32_t gm_tpl8un0qv11eeom_i[] = {0u};
const uint32_t gm_tpl8v9uhia7uc1b_i[] = {0u};
const uint32_t gm_tpl9225uv77leci_i[] = {0u};
const uint32_t gm_tplacjkb64s2e83_i[] = {0u, 1u, 2u};
const uint32_t gm_tplamm5t71udd8k_i[] = {0u, 1u};
const uint32_t gm_tplaqje7ooa5hcl_i[] = {0u};
const uint32_t gm_tplar04pgor24ut_i[] = {0u};
const uint32_t gm_tplb5ldvki5nntc_i[] = {0u, 1u, 2u};
const uint32_t gm_tplba0kefu5gu2c_i[] = {0u};
const uint32_t gm_tplbr5d1kr4l84u_i[] = {0u};
const uint32_t gm_tplbulo51k20om4_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tplceqrbdtcd9p5_i[] = {0u};
const uint32_t gm_tpldbe9bnjeqeqg_i[] = {0u};
const uint32_t gm_tpldiv6dh0or1km_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpldm28gt81dafq_i[] = {0u};
const uint32_t gm_tpldp3bk13rlc44_i[] = {0u};
const uint32_t gm_tpldqjt9kdk4ntt_i[] = {0u};
const uint32_t gm_tple41tg2t64d1i_i[] = {0u, 1u, 2u};
const uint32_t gm_tpled61een8fpfm_i[] = {0u};
const uint32_t gm_tplf41j0oji3nkl_i[] = {0u};
const uint32_t gm_tplf9434bnahqi1_i[] = {0u};
const uint32_t gm_tplfqk35ejq39la_i[] = {0u, 1u, 2u};
const uint32_t gm_tplfqr816g9de0k_i[] = {0u, 1u, 2u};
const uint32_t gm_tplfrm74l83ht8l_i[] = {0u, 1u, 2u};
const uint32_t gm_tplful0p0r5pifs_i[] = {0u, 1u};
const uint32_t gm_tplgp08d1iih1si_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplhrqv5p3pfbh5_i[] = {0u, 1u, 2u};
const uint32_t gm_tpli08vvf1bckj4_i[] = {0u, 1u, 2u};
const uint32_t gm_tpli2vu74n9ss6f_i[] = {0u};
const uint32_t gm_tpli7ohq5o5sj5n_i[] = {0u, 1u};
const uint32_t gm_tplj451rojbvr15_i[] = {0u};
const uint32_t gm_tpljkde9cibn2er_i[] = {0u, 1u};
const uint32_t gm_tplk25d3iiaamil_i[] = {0u, 1u, 2u};
const uint32_t gm_tplkjc1g73er3ub_i[] = {0u};
const uint32_t gm_tpll66vdkdoc48m_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpll6pilv9krmhr_i[] = {0u};
const uint32_t gm_tplld0dt78h0tum_i[] = {0u};
const uint32_t gm_tplmeuuf37dghpi_i[] = {0u, 1u};
const uint32_t gm_tplmghq7vvcvnvi_i[] = {0u};
const uint32_t gm_tplmkb4jrr2jqel_i[] = {0u};
const uint32_t gm_tplmm5mgnjalbfr_i[] = {0u};
const uint32_t gm_tplmpc7ghuokash_i[] = {0u, 1u};
const uint32_t gm_tpln8gtl1e9t7j8_i[] = {0u};
const uint32_t gm_tplnb9cl2s2ukch_i[] = {0u};
const uint32_t gm_tplnc4en2s3t0da_i[] = {0u, 1u, 2u};
const uint32_t gm_tplnt454lmhb7iv_i[] = {0u};
const uint32_t gm_tplorj2ck0ovl7t_i[] = {0u};
const uint32_t gm_tplp98fsht4k706_i[] = {0u};
const uint32_t gm_tplpcjaq35mrruk_i[] = {0u, 1u, 2u};
const uint32_t gm_tplptu6sgqf50o0_i[] = {0u};
const uint32_t gm_tplq54tg5m6aueh_i[] = {0u};
const uint32_t gm_tplqci9e677opav_i[] = {0u};
const uint32_t gm_tplqhugo4i82obv_i[] = {0u, 1u};
const uint32_t gm_tplqn6e8rqmlvpo_i[] = {0u, 1u};
const uint32_t gm_tplqpg9790rv4ki_i[] = {0u};
const uint32_t gm_tplqpi0kdavqkt9_i[] = {0u, 1u};
const uint32_t gm_tplr976guo57cat_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplr9kq0pku58q3_i[] = {0u, 1u, 2u};
const uint32_t gm_tplrgo7romk0g20_i[] = {0u};
const uint32_t gm_tplruf3ms5lqgst_i[] = {0u};
const uint32_t gm_tplsil8jb9ng0ao_i[] = {0u};
const uint32_t gm_tplsk9b8o9jgshf_i[] = {0u, 1u, 2u};
const uint32_t gm_tplska2t1f86rr8_i[] = {0u, 1u};
const uint32_t gm_tplsogg2sp0m9p9_i[] = {0u, 1u};
const uint32_t gm_tplt1lu36t604pk_i[] = {0u};
const uint32_t gm_tplu0rdj3ds7u0l_i[] = {0u, 1u};
const uint32_t gm_tpludgprff7jihr_i[] = {0u};
const uint32_t gm_tpluf8termqcruk_i[] = {0u};
const uint32_t gm_tplugm36655v1ns_i[] = {0u};
const uint32_t gm_tpluguraohl9kn4_i[] = {0u, 1u};
const uint32_t gm_tplun02iesa1r5k_i[] = {0u};
const uint32_t gm_tplup0bf5d2eopf_i[] = {0u};
const uint32_t gm_tplurleasos6378_i[] = {0u};
const uint32_t gm_tplvcccam8nulr1_i[] = {0u, 1u};
const uint32_t gm_tplvflfleui9qjm_i[] = {0u};
const uint32_t gm_tplvrmulr6rg858_i[] = {0u};
const uint32_t gm_tplvv610d042tt6_i[] = {0u, 1u, 2u};
const uint8_t gm_s01aerdf23b33[14] = ".html\"><code>";
const uint8_t gm_s049rlfhk3i2d[6] = "gm_pr";
const uint8_t gm_s08dcqdp58r5f[3] = ")[";
const uint8_t gm_s091lteo20dar[64] = "`m.select(a, b)` takes vectors of {} lanes, like `{}`; not `{}`";
const uint8_t gm_s0a0jsggm04iu[8] = "gm_kind";
const uint8_t gm_s0ad3307s8jmo[105] = "a rest parameter is one name with an unnamed tuple type or a type parameter, and no default: `args: ..A`";
const uint8_t gm_s0bpji6inee0r[19] = "no element named `";
const uint8_t gm_s0e9nt7u0mvf9[66] = "a `ctl` operation was performed while a fiber was being discarded";
const uint8_t gm_s0i56e621jvcv[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\357\001\000\0005\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s0ibfi39mcc7n[9] = "` clause";
const uint8_t gm_s0j5c49gnifu8[18] = "F.h.clause_lit = ";
const uint8_t gm_s0l72j5pkqi6s[9] = "`resume`";
const uint8_t gm_s0mpcnom7nq2p[47] = "an array can be indexed only by an enum, and `";
const uint8_t gm_s0mph2pjoa3np[17] = "` (required by `";
const uint8_t gm_s0nnk79up541r[32] = "invalid memory access: address ";
const uint8_t gm_s0ph7fnc0ftge[16] = "gm_fs_close_dir";
const uint8_t gm_s0q6kpmhsprri[33] = "shift amount {} too large for {}";
const uint8_t gm_s0rfpnv4c3k1l[38] = "this `cancel` ends the handler clause";
const uint8_t gm_s0ug1ukauk3l9[74] = "a float literal cannot have the type `{}` (in an instantiation used here)";
const uint8_t gm_s0ve0dba4rtj8[13] = "assign-tuple";
const uint8_t gm_s14lpmr3v2vk3[57] = "`{}` does not satisfy `{}({})` required by this function";
const uint8_t gm_s15i3qlq36ep1[16] = "uint8_t pe[{}];";
const uint8_t gm_s16bsnuu7i04s[3] = "ms";
const uint8_t gm_s1a1r1qe3ssot[9] = "__int128";
const uint8_t gm_s1d673tn03i3r[5] = "] = ";
const uint8_t gm_s1ekjot5tdpje[67] = "positional arguments must come before a spread that supplies names";
const uint8_t gm_s1gf327eapo1j[11] = "ProcLaunch";
const uint8_t gm_s1kvbdtols1cc[53] = "internal error: break to a label that is not running";
const uint8_t gm_s1p5ld7i8mkfu[10] = " (\302\24719.6)";
const uint8_t gm_s1q0n6t6d7g6n[34] = "range operators cannot be chained";
const uint8_t gm_s1qfo73r5cemc[64] = "gm_fiber *{} = gm_fiber_new(&{}, (uint8_t *){}, {}, {}, 0, {});";
const uint8_t gm_s1r46q0dlpfeo[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\375\002\000\000T\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s2053hhvf16fm[2] = "]";
const uint8_t gm_s21bv1o9t2tge[11] = "gm_variant";
const uint8_t gm_s2254b9hkee3b[6] = "&gm_r";
const uint8_t gm_s25eqlrovn9m2[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\0009\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s26o5frgr2hds[7] = "git = ";
const uint8_t gm_s27pt4vcvtscl[24] = "write `a < b and b < c`";
const uint8_t gm_s2bjh3dbncrc8[3] = "\">";
const uint8_t gm_s2e5tbp7977b1[11] = "gm_u64_dec";
const uint8_t gm_s2gjlt9eraf0q[10] = "{} :: ...";
const uint8_t gm_s2hbi2g88pale[19] = "gm_unw = gm_saved;";
const uint8_t gm_s2j1m46v0uc4p[10] = "black_box";
const uint8_t gm_s2jl146j9vpnj[20] = "{} cannot be hashed";
const uint8_t gm_s2kmbpuutp9qq[5] = "Addr";
const uint8_t gm_s2odv5tht4lj8[20] = "compiler/lsp/lsp.gm";
const uint8_t gm_s2u5mf7vjj6ie[24] = "({})(({}){} << (int){})";
const uint8_t gm_s2vme195ddtm7[25] = "slices have no method `.";
const uint8_t gm_s2vmmrs8i9iqs[4] = ")\012\012";
const uint8_t gm_s31c4u1mtb865[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000s\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s33ft5ck7snhl[4] = " @ ";
const uint8_t gm_s34n6pusofh71[8] = "tag of ";
const uint8_t gm_s37v6u2dqp80f[5] = "exit";
const uint8_t gm_s38dtnjej3849[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\347\000\000\000\037\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000";
const uint8_t gm_s3bb7jrtcd5ot[15] = "-ftree-bit-ccp";
const uint8_t gm_s3dk9gdukms7b[18] = "#line GM_C_SOURCE";
const uint8_t gm_s3hhdo4dfru76[7] = "ld.lld";
const uint8_t gm_s3hinmnodmkmv[21] = " invalid for length ";
const uint8_t gm_s3ilkivmnrfk4[28] = "` leaves the directory of `";
const uint8_t gm_s3k9reqbrnuns[25] = "`{}` takes {} argument{}";
const uint8_t gm_s3kfpoo7v2f2o[27] = "({}({}, {}) || {}({}, {}))";
const uint8_t gm_s3lbmpmt95hvh[7] = "update";
const uint8_t gm_s3m769if3iik9[32] = ", NULL, 0, 0 }; gm_bt_top = &B;";
const uint8_t gm_s3mnje3d1c6qa[27] = ".event == GM_EV_PERFORM) {";
const uint8_t gm_s3mqrb8c7qaar[9] = "\077\000\000\000\000\000\000\000";
const uint8_t gm_s3n3t2qjk9dg2[5] = " %= ";
const uint8_t gm_s3ndsddjcvskd[3] = "[]";
const uint8_t gm_s3r7cg0hbrkdg[17] = "error-union-type";
const uint8_t gm_s3s76fqo1a9jk[24] = "compiler/pkg/archive.gm";
const uint8_t gm_s45fljr5t8358[4] = ", a";
const uint8_t gm_s47ic12e9ejh1[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\214\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s4a6h3o2n8s33[94] = "gm_node {}; {{ gm_node *f = gm_find({}, {}); if (f) {{ {} = *f; {}.next = {}; {} = &{}; }} }}";
const uint8_t gm_s4c0t3s0kj31v[12] = "this unwrap";
const uint8_t gm_s4fi69869ooj8[28] = "` together have lanes 0 to ";
const uint8_t gm_s4g3rq58567m6[8] = "LiftErr";
const uint8_t gm_s4gc37vr9terk[58] = "`: `checksum` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_s4i05mf7gcfb7[17] = "struct {} *gm_Fp";
const uint8_t gm_s4jglh3q4fib8[12] = "vm_assemble";
const uint8_t gm_s4jhjvgae4896[13] = "net_shutdown";
const uint8_t gm_s4k55k154s62m[120] = "use `{}` or `{:spec}` for an argument, `{0}` or `{0:spec}` for the value with that number, and `{{` for a literal brace";
const uint8_t gm_s4k57chtk2mkc[88] = "(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s)";
const uint8_t gm_s4m9nol66dh0a[25] = " are not ordered (\302\2477.5)";
const uint8_t gm_s4n5m9kt2s9ld[27] = "textDocument/signatureHelp";
const uint8_t gm_s4nddosr4taav[5] = "'\\u{";
const uint8_t gm_s4pmpk07ugvq6[5] = "last";
const uint8_t gm_s4ql65vkv7h9p[13] = "void *gm_out";
const uint8_t gm_s4vgbnvmvj0st[34] = "if this is deliberate, name it `_";
const uint8_t gm_s511vp5vjnnnk[68] = "`@caller_location()` is allowed only as a parameter's default value";
const uint8_t gm_s523ivho5m3dq[13] = "thread_local";
const uint8_t gm_s541nrvershlm[7] = "is_inf";
const uint8_t gm_s5ed1f66u9mdp[28] = "` to the function's effects";
const uint8_t gm_s5el4r6boe881[14] = " is freed at ";
const uint8_t gm_s5fcnrkaual42[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000^\002\000\000*\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s5fk67ihob1ic[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000}\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s5ocg5vi2qib3[21] = "patch version number";
const uint8_t gm_s5ojatldfpfs2[8] = "[build]";
const uint8_t gm_s5p7bqbmkba97[13] = "Would remove";
const uint8_t gm_s5tgvdli5u3js[17] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s5v003pfcdo1r[22] = "if (GM_LD(uintptr_t, ";
const uint8_t gm_s6077qnlc299h[4] = "-lm";
const uint8_t gm_s684ivkkpni41[7] = ":reset";
const uint8_t gm_s68voc4rs92m2[15] = ",\"end_column\":";
const uint8_t gm_s6ac0uac1dtsl[62] = "{}: c-sources of package `{}` lists `{}`, which is not a file";
const uint8_t gm_s6b2qpq3tgsj8[12] = ", sha256 = ";
const uint8_t gm_s6c128h826nsg[25] = "compiler/interp/calls.gm";
const uint8_t gm_s6cs6skh2j334[17] = "FmtSpec { fill: ";
const uint8_t gm_s6f3urtqqu41d[16] = " digits after `";
const uint8_t gm_s6i1ull1350kv[5] = "`.!`";
const uint8_t gm_s6igeulorpagn[14] = "gm_clock_wall";
const uint8_t gm_s6ket4hq2umi0[51] = "a function pointer type has one calling convention";
const uint8_t gm_s6nm27ehg597q[7] = "FsSync";
const uint8_t gm_s6qvm08vrap69[4] = ", (";
const uint8_t gm_s6t4r831mukdr[3] = ";\012";
const uint8_t gm_s6tv3tkgkodqm[12] = "` of type `";
const uint8_t gm_s6v08aos37nfe[8] = "profile";
const uint8_t gm_s70bck1t85qpv[9] = "\\x{:02x}";
const uint8_t gm_s71j6n6s8up14[17] = "remove the label";
const uint8_t gm_s7345rpped3bc[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\0005\000\000\000\034\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s736j3npg82a4[6] = "<end>";
const uint8_t gm_s756ctefg4er5[59] = "` is a function, which is not a value to format (\302\24721.6.1)";
const uint8_t gm_s75snq6p722c8[19] = "cannot assign to `";
const uint8_t gm_s7bqfholfgtfq[13] = "    goto {};";
const uint8_t gm_s7ddabri39fvk[9] = "call fn ";
const uint8_t gm_s7g0chs5u30do[21] = "cannot remove {}: {}";
const uint8_t gm_s7h5sk98p4bgn[17] = "gm_format_spec(&";
const uint8_t gm_s7hi4f3a8v74u[13] = "inst->result";
const uint8_t gm_s7hkl5mqa15gm[5] = " in ";
const uint8_t gm_s7ifmhl5viqvs[10] = "keyword `";
const uint8_t gm_s7jhnrnbmkbbq[39] = "if (GM_LD(uintptr_t, (const uint8_t *)";
const uint8_t gm_s7l31qd1tkp00[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\247\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s7s25m3nlrif2[6] = "Float";
const uint8_t gm_s809b2tff170u[13] = "gm_table_get";
const uint8_t gm_s82anase9fhnd[8] = "{} l{};";
const uint8_t gm_s82d2namrcvs3[17] = "__builtin_isnan(";
const uint8_t gm_s8575j3cip1vh[36] = " == 0) gm_trap(\"division by zero\", ";
const uint8_t gm_s86uh6df2eunn[20] = "with @{} label {}{}";
const uint8_t gm_s88rffcs9cj2p[12] = "std/process";
const uint8_t gm_s89md2uvrtjfh[14] = "gm_trap_bytes";
const uint8_t gm_s8b01h2tetj3e[45] = " has no default branch to use (required by `";
const uint8_t gm_s8cup0kpdd4dj[14] = "invalid input";
const uint8_t gm_s8f9rv6hrr024[11] = "dependency";
const uint8_t gm_s8guqcjcce9oc[22] = "a mask (`bool` lanes)";
const uint8_t gm_s8h39pil8lgh0[41] = "every field of a `#c_union` needs a type";
const uint8_t gm_s8ivbu50c9tsq[9] = " \000\000\000\000\000\000\000";
const uint8_t gm_s8mnqic20r702[7] = "&quot;";
const uint8_t gm_s8mre3qset1hp[60] = "expected a function with {} parameters, but this one has {}";
const uint8_t gm_s8pvtaofn65p9[29] = ") == 0 && __builtin_signbit(";
const uint8_t gm_s8ta4feq24fk9[12] = "step budget";
const uint8_t gm_s967qbtu9ltck[12] = "#vec [3]f32";
const uint8_t gm_s98se7jporda2[72] = " postfix operations in a row, the limit of this implementation (\302\24723.5)";
const uint8_t gm_s9beklm4tc5k5[193] = "\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s9bnhibq0fug8[5] = " - (";
const uint8_t gm_s9e7jg1nv469k[6] = "gm_ll";
const uint8_t gm_s9ff7kbg5s1vv[10] = " << (int)";
const uint8_t gm_s9iu5gsqog7uc[5] = "hash";
const uint8_t gm_s9lclteeapkg2[3] = "-0";
const uint8_t gm_s9md3pg6vlt5h[3] = "# ";
const uint8_t gm_s9mhd8h7bolj3[48] = "`--older-than` needs a number of days, not `{}`";
const uint8_t gm_s9nk06pmq8hl1[34] = "gamma.toml has no dependency `{}`";
const uint8_t gm_s9ok67plsnbj1[7] = "--refs";
const uint8_t gm_s9pdhp63uj9bd[7] = " s/run";
const uint8_t gm_s9rhhj2j5hs2v[36] = "`{}` is bound twice in this pattern";
const uint8_t gm_sa0ep7oknkrm1[18] = "discard the value";
const uint8_t gm_sa15idp716r2q[12] = "    print(\"";
const uint8_t gm_sa36vve0410hq[7] = "Darwin";
const uint8_t gm_sa3obh41s8f7p[70] = "`, which depends on this value: the declarations form a cycle (\302\2473.4)";
const uint8_t gm_sa42h1oh66nds[9] = "ptr_bits";
const uint8_t gm_sa6br7uea4stj[69] = "    uintptr_t na = {}, nb = {};\012    const uint8_t *pa = a, *pb = b;\012";
const uint8_t gm_sa70lf0p32gn5[28] = "`not` takes a `bool`, not `";
const uint8_t gm_sa9o54tgja188[12] = "powerpc64le";
const uint8_t gm_sa9ukgikn2pki[26] = "`{}` is not a struct type";
const uint8_t gm_sah1jmr01nidj[5] = "Free";
const uint8_t gm_sahahh1a40c5c[37] = "missing argument for the parameter `";
const uint8_t gm_sahjmrj1mjf77[8] = "{}{}.gm";
const uint8_t gm_sajbccb18c45d[6] = "Quote";
const uint8_t gm_sajo3kdi51jpa[10] = "bits = {}";
const uint8_t gm_sam1nkbp8dhs9[16] = "if ((uintptr_t)";
const uint8_t gm_saq7q763oq882[48] = "{} is not a directory (`gamma new` creates one)";
const uint8_t gm_sar5aak6f8jpo[44] = "internal error: return from a missing frame";
const uint8_t gm_sarlhchtvv77m[13] = "--show-error";
const uint8_t gm_sasg9nroe4gk1[4] = "fma";
const uint8_t gm_sasocllalvlsg[7] = "|{}:{}";
const uint8_t gm_sb02cov56i977[8] = ", cx);\012";
const uint8_t gm_sb2rimopi40u7[39] = "    uint64_t n = GM_LD(uintptr_t, v + ";
const uint8_t gm_sb5j6apm5mfoq[31] = "` already exists in gamma.toml";
const uint8_t gm_sb71rivinn7pn[10] = "op {}.{}\012";
const uint8_t gm_sb7kik80lfbge[11] = "{}:{}:{}: ";
const uint8_t gm_sb88rajuutaep[4] = "esc";
const uint8_t gm_sbabluaguu2bc[51] = "invalid template (in an instantiation used here): ";
const uint8_t gm_sbg66h7153j4l[16] = "`#bits` structs";
const uint8_t gm_sbgcf6rjaqh5h[8] = " {\012    ";
const uint8_t gm_sbgepjj6hgjej[4] = "Set";
const uint8_t gm_sbo1c1kpq53tj[31] = "to close the pattern arguments";
const uint8_t gm_sbuhie0vb9t62[9] = "pat-wild";
const uint8_t gm_sc17hqmokdbc2[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\364\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sc3pq8972q5i0[15] = "is_signed = {}";
const uint8_t gm_sc5qopgd7dm66[8] = "Display";
const uint8_t gm_scacg345bm166[69] = "`#align` goes directly before a `struct` or `union` literal (\302\24720.2)";
const uint8_t gm_sce9ak22luehr[27] = "#define GM_FREESTANDING 1\012";
const uint8_t gm_scfama5f5jm1j[25] = "__builtin_unreachable();";
const uint8_t gm_scfp9v0kro0hh[17] = "\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_scggaq5euuhe7[19] = "  at <repl>:{}:{}\012";
const uint8_t gm_sciqh24mne54a[4] = "<p>";
const uint8_t gm_sckqm6be9dvme[13] = "[profile.{}]";
const uint8_t gm_scl6is0vlr1m0[62] = "`{}` is not a library name: use letters, digits and `_ . + -`";
const uint8_t gm_scmcg8350emvu[19] = "invalid template: ";
const uint8_t gm_scttf7jtr4fn9[6] = "gm_cs";
const uint8_t gm_scumr3cdeol3i[13] = "grow_mem_buf";
const uint8_t gm_scvd4ia8nkih4[17] = "\000\000\000\000\000\000\000\000'\000\000\000\000\000\000\000";
const uint8_t gm_scviua4g9o7rp[62] = "`{}` is a variable, so its value is not known at compile time";
const uint8_t gm_sd0rfmo1vm5au[12] = "alloc_check";
const uint8_t gm_sd2ioiubqiqs5[46] = "({} > {} \077 ({}){} - ({}){} : ({}){} - ({}){})";
const uint8_t gm_sd5r0412dj6fb[15] = "unwrap of null";
const uint8_t gm_sd6t9e70pfcub[29] = "the exponent is larger than ";
const uint8_t gm_sd9regb2pbgv1[8] = "payload";
const uint8_t gm_sdajtpr06648l[6] = " file";
const uint8_t gm_sdbvbbg6vc50j[27] = "-Wl,--thinlto-cache-dir={}";
const uint8_t gm_sdd9j9qgq2abj[29] = "memcpy(&{}, &{}, sizeof {});";
const uint8_t gm_sddmbtkcc2n83[11] = "#simd [{}]";
const uint8_t gm_sdeu8nuc597e8[19] = "__atomic_fetch_sub";
const uint8_t gm_sdft8ov6bb7s6[8] = "Checked";
const uint8_t gm_sdhlphf1ucpbo[18] = "    return GM_LD(";
const uint8_t gm_sdj9af8hu07uk[18] = "constructing `{}`";
const uint8_t gm_sdjfsrjr2b2ke[2] = "r";
const uint8_t gm_sdqn7n16togpr[29] = " must be an array of strings";
const uint8_t gm_sdsb9longvn22[40] = "not supported by the C back end yet: {}";
const uint8_t gm_sdu3h28udk8s9[13] = "gm_mix64(h, ";
const uint8_t gm_sdu7bd5v9q750[65] = "static bool {}(gm_node *cx, const gm_type *d, const void *v, {})";
const uint8_t gm_sduk7s8m3amo6[12] = "intrinsic @";
const uint8_t gm_se1jf3monjum8[10] = "--recover";
const uint8_t gm_se23d1hoq85r1[5] = "data";
const uint8_t gm_se3cg8f7o7u1d[12] = "gm_buf_push";
const uint8_t gm_se7uf9ek5fjqq[18] = "gm_trap_hook_call";
const uint8_t gm_sear9h0hhleaj[6] = "lc_br";
const uint8_t gm_secneq49c8dtf[36] = "` (`gamma lint --rules` lists them)";
const uint8_t gm_selmtbgalekup[10] = " {} {}{};";
const uint8_t gm_semec5mr3jo2t[73] = "`{}` has no methods; `a[..]` is a slice of its elements, which has `.{}`";
const uint8_t gm_semskfu4esg5u[17] = "a name after `\077`";
const uint8_t gm_sepu95c7o773o[6] = "Cover";
const uint8_t gm_seqv335lv6284[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\224\002\000\000\015\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_serk0pt9jcvpr[14] = "GAMMA_RUNTIME";
const uint8_t gm_sesqjijoh7km4[9] = " \302\265s/run";
const uint8_t gm_setdfq42nhg1q[5] = "'\\r'";
const uint8_t gm_sf20puqngp9vm[9] = "index.{}";
const uint8_t gm_sf37jkpot59cu[54] = "_Static_assert(sizeof({}) == {} && _Alignof({}) == {}";
const uint8_t gm_sf4qubhm4h2hm[32] = "this use fixed the type as `{}`";
const uint8_t gm_sf5jsgjp29cqn[14] = "thread_detach";
const uint8_t gm_sf9nvp09l65v3[17] = "integer too long";
const uint8_t gm_sfcgvtliokb7a[28] = ", so this spread must have ";
const uint8_t gm_sfd2hpva2vj33[20] = "<unprintable error>";
const uint8_t gm_sfeb57kq1r2n4[4] = ".. ";
const uint8_t gm_sfflovsm8rlpo[51] = "`#soa` allocation is missing from the core library";
const uint8_t gm_sfg8mkf2826qa[5] = " += ";
const uint8_t gm_sfggcm2ak7679[20] = "handler-ref {} [{}]";
const uint8_t gm_sfhb5i19r421v[6] = "<h{}>";
const uint8_t gm_sfled84a022e2[50] = "`.make(n)` makes a `#soa` slice; `{}` is an array";
const uint8_t gm_sfrcaft0lo33k[60] = "only `@static_assert` may appear as a top-level declaration";
const uint8_t gm_sfrrtib86oiig[23] = "(intptr_t)((uintptr_t)";
const uint8_t gm_sfve13r15sgkv[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000#\002\000\000/\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sfvspsnv23gbt[7] = "subdir";
const uint8_t gm_sg25rp8qmrbme[57] = "unknown lint rule `{}` (`gamma lint --rules` lists them)";
const uint8_t gm_sg58j44mq0qb5[61] = "`{}` is a compile-time value parameter, but a type was given";
const uint8_t gm_sg90up8jjg51i[15] = "fmt_float_spec";
const uint8_t gm_sga7ge5dc8qdr[50] = "`@variants` takes a tagged union type, not `{}`{}";
const uint8_t gm_sgal4nbdtbtvo[18] = "gm_buf_free(&{});";
const uint8_t gm_sgbib360sn2mu[131] = "this conversion gives the literal's variable the type `{}` (\302\2476.5); `.trunc({})` or `.checked({})` would convert from its own type";
const uint8_t gm_sgbklk5omjecv[29] = "` does not apply to a struct";
const uint8_t gm_sgemh066sqogf[4] = "<<%";
const uint8_t gm_sgg5s9bhkgqj3[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\335\000\000\000'\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sgkuqlapm8vur[12] = "`~` on `{}`";
const uint8_t gm_sgnt3t4lgofos[35] = "` differ and are not both pointers";
const uint8_t gm_sgooul7ab8sa1[65] = "invalid free of {:#x}: the heap block there has {} bytes, not {}";
const uint8_t gm_sgpahsg79gte4[26] = "expected `{}`, found `{}`";
const uint8_t gm_sgrkf3bk77nul[17] = "__builtin_inf())";
const uint8_t gm_sgrv5qufpkvf8[7] = "</dl>\012";
const uint8_t gm_sgtlutvecf7kf[46] = "cannot run {}: the process cannot be started\012";
const uint8_t gm_sh1e27cf0k9if[6] = "{}:;\012";
const uint8_t gm_sh5prd89hpoup[85] = "`@fiber` takes a function pointer, its argument and a stack: `@fiber(f, arg, stack)`";
const uint8_t gm_sh7dg9dli11hk[50] = "`.shuffle` makes a vector of one lane per index: ";
const uint8_t gm_sh7ob55fi0i62[26] = "to close the array length";
const uint8_t gm_sha819aic5lkk[22] = "no element named `{}`";
const uint8_t gm_shakeh6dfb84i[17] = "Content-Length: ";
const uint8_t gm_sheb91gbrs2c3[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000L\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_shesnhdok5fgk[55] = ") gm_trap(\"float to integer conversion out of range\", ";
const uint8_t gm_shg24ogv0rnt4[14] = "the results `";
const uint8_t gm_shjri07u8kc84[27] = "gm_unw.code = (uint8_t){};";
const uint8_t gm_shl5hekt9ba4l[19] = "calling this value";
const uint8_t gm_shmknkannurll[5] = "bind";
const uint8_t gm_shmpls8n0g8h9[4] = " |\012";
const uint8_t gm_shn9bhhpahcjo[16] = "` matches only ";
const uint8_t gm_shne2m0cj5kgi[6] = "floor";
const uint8_t gm_shnk1stovqggp[5] = "<th>";
const uint8_t gm_shqein5ip3gpn[19] = "completionProvider";
const uint8_t gm_shqvr1vn78f0b[54] = "typedef struct {{ _Alignas({}) uint8_t b[{}]; }} {};\012";
const uint8_t gm_shsdmcj9g7u41[6] = "gm_bt";
const uint8_t gm_shsrp9mgu2tm3[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000E\001\000\000S\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sht5iivcbi64u[12] = "` requires ";
const uint8_t gm_shtuc6se4lilq[7] = "EnvVar";
const uint8_t gm_shv1k5m05egbe[6] = "<tmp>";
const uint8_t gm_si2pj4mpn1kn5[11] = "for (;;) {";
const uint8_t gm_si3s44ll3u1p2[42] = "cannot infer which `{}` this performs; {}";
const uint8_t gm_si56o26p8hqsl[8] = "ppc64le";
const uint8_t gm_si5gf6931vagv[11] = "type-name ";
const uint8_t gm_si68bogf7eu32[8] = "_result";
const uint8_t gm_si6fvebhspqjd[15] = "</strong></em>";
const uint8_t gm_si6ooauhec559[5] = "lane";
const uint8_t gm_si7vj1dcaqhfd[5] = "[{}]";
const uint8_t gm_sica240oidke0[8] = "({})~{}";
const uint8_t gm_sidmu4drv3rks[19] = "Connection refused";
const uint8_t gm_sie82oc0ua5dm[3] = ">\012";
const uint8_t gm_sif4kr35r5dhh[81] = "unknown calling convention `.{}`; the conventions are `.C`, `.SysV` and `.Win64`";
const uint8_t gm_siflgunfq3r7b[11] = "an integer";
const uint8_t gm_sik1l49uru5dh[13] = "thread_yield";
const uint8_t gm_sims8gu714a9a[14] = "-Wl,-rpath,{}";
const uint8_t gm_siqgrmkn8n4vn[8] = "FsClose";
const uint8_t gm_siuqnp32vjkps[8] = "gm_spec";
const uint8_t gm_sj0tu4mea4egk[5] = " & ~";
const uint8_t gm_sj21lfdl9q0t4[6] = "#soa ";
const uint8_t gm_sj2f7h0km68ut[32] = "`#trap_hook` takes no arguments";
const uint8_t gm_sj2jft9n0jm50[3] = "\\'";
const uint8_t gm_sj3cmvbmi7cm6[9] = "template";
const uint8_t gm_sj7osq6sb8i2r[108] = ") is not in gamma.lock or the lock is out of date; it cannot be resolved with --offline (run `gamma fetch`)";
const uint8_t gm_sjb713eltfa88[112] = "this call is not an iterator (it returns `{}` and yields nothing), but the parameter takes an iterator (\302\24713.5)";
const uint8_t gm_sjbjjlvp0cfb7[12] = "rotate_left";
const uint8_t gm_sjc00l2ljprs2[4] = "~{}";
const uint8_t gm_sjcgs2v314rl7[19] = "gamma: cannot run ";
const uint8_t gm_sjcosulco987j[98] = "names not written in the usual case: types and variants in UpperCamelCase, the rest in snake_case";
const uint8_t gm_sjg1ri23hm753[6] = "local";
const uint8_t gm_sjl0s4joed61f[15] = "({})({} {} {})";
const uint8_t gm_sjljm9omp36ob[52] = "`{}` is an effect set, not a single effect (\302\24712.3)";
const uint8_t gm_sjpstm6i2bq0k[417] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sjvgkv27ovi7g[48] = "`v` and `w` of `{}` together have lanes 0 to {}";
const uint8_t gm_sk2tgsi47prt7[14] = "_Bool is_err;";
const uint8_t gm_sk6ueggk5ji6v[10] = "operation";
const uint8_t gm_skapbb3maknat[5] = "Bits";
const uint8_t gm_skdv3lmatcupg[21] = "static __attribute__";
const uint8_t gm_skf282h8e3q1e[49] = "cannot run {}: the process cannot be waited for\012";
const uint8_t gm_skfdav0cuo9ri[14] = "perform {} {}";
const uint8_t gm_skfrgfcfg8vrf[36] = "internal error: a fiber disappeared";
const uint8_t gm_skh04d6p3n402[79] = "`resume` expects a continuation call with at most one value: `resume k(value)`";
const uint8_t gm_skh4rkm9k1elb[8] = "</code>";
const uint8_t gm_skjltb6l69n5p[11] = "negating `";
const uint8_t gm_sktjfeu2a0ava[15] = "union {{ {} }}";
const uint8_t gm_sl0oflhpdomi7[5] = "fast";
const uint8_t gm_sl34v6fvasecs[31] = "to close the clause parameters";
const uint8_t gm_sl41530g3ehjt[8] = "gm_cstr";
const uint8_t gm_slff7lshtgu6r[42] = "dependency cycle between declarations: {}";
const uint8_t gm_slgnoj3l4ks2a[17] = "gm_fs_remove_dir";
const uint8_t gm_slhllpfip1vpg[3] = " l";
const uint8_t gm_slipvb5272ocn[4] = ")))";
const uint8_t gm_slj1im0lsqj9h[32] = "`else` on optionals and results";
const uint8_t gm_slk0i3scss4m6[11] = "gm_wsa_err";
const uint8_t gm_sll03ulrg0frn[81] = "a second-class function cannot be {}; use a function pointer `^fn(\342\200\246)` (\302\24711.6)";
const uint8_t gm_slpekm89q5lb3[29] = "empty identifier segment in ";
const uint8_t gm_slrfdinnoh7ti[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\024\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sltqebpmj61tp[3] = "us";
const uint8_t gm_sluco9qbar70k[10] = "aggregate";
const uint8_t gm_sm03uudimlmgi[59] = "the operands of `{}` have different types: `{}` and `{}`{}";
const uint8_t gm_sm7p819bg8637[13] = "string_value";
const uint8_t gm_sm9m81v5v04r9[12] = "gm_node *n;";
const uint8_t gm_smab0ikt4jd4g[17] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_smaguor10so8u[12] = "=http,https";
const uint8_t gm_smarqt2mi26ea[7] = "{}.len";
const uint8_t gm_smdjnv4d2cfrh[31] = " through a different allocator";
const uint8_t gm_smerqm8g793l6[2] = "&";
const uint8_t gm_smgfld86ipv3f[58] = "; a scalar is not spread over the lanes: use `V(fill: x)`";
const uint8_t gm_smhebipq24jdo[28] = "EOF while parsing an object";
const uint8_t gm_smimi08sfrg0n[5] = "path";
const uint8_t gm_smos9ggka3uc3[10] = "<session>";
const uint8_t gm_smpdttgra9a1b[85] = "a thread runs a `^fn(arg: rawptr) needs Sys` with the C calling convention, not `{}`";
const uint8_t gm_smurk4n2ra6mi[11] = "this index";
const uint8_t gm_smv96er1uehm8[94] = "a function pointer performs only the effects in its type, and `{}` is not among them (\302\24711.7)";
const uint8_t gm_smviu3logd6du[4] = "; i";
const uint8_t gm_sn0785fueq6ki[64] = "internal error: formatting would change token {} ({} became {})";
const uint8_t gm_sn12gub8tjj5h[6] = "zero:";
const uint8_t gm_sn3tcnrle7f4r[4] = ":\012\012";
const uint8_t gm_sn4gb2qts08ck[14] = ", inclusive: ";
const uint8_t gm_sn5m7a373k04g[45] = "compile-time values computed from parameters";
const uint8_t gm_sn6sqlran7h3r[7] = "resume";
const uint8_t gm_sn8v0ub9fm41i[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\024\000\000\000_\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_snboa22b96bbc[4] = "dir";
const uint8_t gm_snc5ns3akncuq[6] = "count";
const uint8_t gm_snf877edekrff[11] = "  = note: ";
const uint8_t gm_snftglmif3ta2[11] = "underscore";
const uint8_t gm_snl3g51f7tu9o[12] = "upper_bound";
const uint8_t gm_snoq0o06l5uns[15] = "try {} lift {}";
const uint8_t gm_snqbu1qqmr5lp[4] = "sub";
const uint8_t gm_snr2l2v2pqfua[4] = "vec";
const uint8_t gm_snseiioj5sbgs[2] = "F";
const uint8_t gm_sntddact5d6vp[4] = "*{}";
const uint8_t gm_so26uaund1443[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000g\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_so2pnermu207h[23] = "`{}` is declared twice";
const uint8_t gm_so3hnoui8sg80[12] = "`@variants(";
const uint8_t gm_so3qrsvnd4euf[12] = "nfields = 0";
const uint8_t gm_so4mj8r7k5aek[3] = "0o";
const uint8_t gm_so52klh2mgoce[23] = "cannot download {}: {}";
const uint8_t gm_so597te7oecin[72] = "dependency `{}` of `{}`: the path `{}` does not exist in the repository";
const uint8_t gm_sobnp9i68hvdj[16] = "--no-same-owner";
const uint8_t gm_sodi7jjra685n[6] = "gm_wb";
const uint8_t gm_soeesub7ccc96[31] = "argument {} is not valid UTF-8";
const uint8_t gm_sogv4el7uffui[58] = "invalid mode `{}` in {}: use debug, checked, safe or fast";
const uint8_t gm_soi4hbchhjloe[4] = "var";
const uint8_t gm_soka1k58t5da3[16] = "a copy of gamma";
const uint8_t gm_sop5f3b03uipi[9] = "readlink";
const uint8_t gm_soqhrfmr1feii[7] = " run, ";
const uint8_t gm_sor8ce07jk3r2[10] = "--profile";
const uint8_t gm_sovn1qud9dmh0[70] = "` is a compile-time parameter, whose value is not known here (\302\24716.5)";
const uint8_t gm_sp0ag7curvl4o[42] = "internal error: a copy between non-slices";
const uint8_t gm_sp3vt8erg7nae[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\206\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sp4426g665gvh[53] = "}} else if ({}.event_fiber == {} && !gm_unw.kind) {{";
const uint8_t gm_sp7fbepv94vjk[3] = "es";
const uint8_t gm_sp8ppesjprts0[67] = "), and --locked was given; run `gamma fetch` and commit gamma.lock";
const uint8_t gm_spa4t2l419v39[14] = " parameter(s)";
const uint8_t gm_spfda095eoqse[34] = "parameter types are required here";
const uint8_t gm_spki3o9t7n682[4] = "own";
const uint8_t gm_spm0cds2upaql[7] = "` are ";
const uint8_t gm_spocqdbj2524b[20] = "Directory not empty";
const uint8_t gm_spqmq1ln67me5[79] = "gamma: cannot start the language server process; serving without a supervisor\012";
const uint8_t gm_sps7gfr3iknaa[5] = "let ";
const uint8_t gm_spvfph5mfb023[16] = "(uint8_t)({}{})";
const uint8_t gm_spvg0rt0dloah[9] = "callconv";
const uint8_t gm_sq0dqiq83b924[9] = "function";
const uint8_t gm_sq64eqmcuoej6[15] = " values, not `";
const uint8_t gm_sq75i0jov46r1[63] = "{}(GM_LD(void *, &{}), GM_LD(uintptr_t, (uint8_t *)&{} + {}));";
const uint8_t gm_sq9jk07jm2tgi[10] = ".align = ";
const uint8_t gm_sq9ngpqr0m7ek[31] = "unterminated character literal";
const uint8_t gm_sqb8noo4kgsmi[12] = "gm_sink_put";
const uint8_t gm_sqfrv3g5v9m2n[5] = "load";
const uint8_t gm_sqkn3op33hp4i[24] = ", __ATOMIC_RELAXED)) {}";
const uint8_t gm_sql0in48r61nd[5] = " || ";
const uint8_t gm_sql5f30410gqn[20] = "gamma: `--run-test=";
const uint8_t gm_sqlp5qu42o606[11] = "`x.{}({})`";
const uint8_t gm_sqmd0r48ameas[7] = "({}{})";
const uint8_t gm_sqqlqus41v6vq[12] = "GM_ZERO({})";
const uint8_t gm_sqr7arfv61pma[9] = "\000\000\000@\000\000\000\000";
const uint8_t gm_sqri8tum3tpfm[3] = "Eq";
const uint8_t gm_squedv51820an[5] = "null";
const uint8_t gm_sr2oeunjjbqtf[14] = "fields = {}_f";
const uint8_t gm_sr2qa646easkn[56] = "`: it is declared in a dependency, outside this package";
const uint8_t gm_sr3et5tffndla[28] = "cannot run the C compiler `";
const uint8_t gm_sr3suba4tbk8h[33] = "0123456789abcdefghijklmnopqrstuv";
const uint8_t gm_sr486u97qk9sk[13] = "math vectors";
const uint8_t gm_sr54681eccerj[11] = "module {}\012";
const uint8_t gm_sr5hmn4fnbh3s[3] = "_f";
const uint8_t gm_sr82af5e87dso[54] = "internal error: no handler declaration for the effect";
const uint8_t gm_sr8o3r2mb0he1[81] = "this returns {} a temporary array, which dies when the function returns (\302\24715.1)";
const uint8_t gm_srd2ukct5o1qh[6] = ".tmp-";
const uint8_t gm_sri0h4dm5d3ul[17] = "a parameter name";
const uint8_t gm_srjo6vku5gka5[20] = "the other candidate";
const uint8_t gm_srlklqc70q6ju[3] = "_c";
const uint8_t gm_srltb43nuabaq[5] = "item";
const uint8_t gm_srrjdhjujsp18[16] = "for (uint32_t k";
const uint8_t gm_srrqh3pev0igg[9] = "an array";
const uint8_t gm_srsr8fvlin2g6[65] = "too few unicode value digits, expected unicode hexadecimal value";
const uint8_t gm_ss3f6irsroj9l[37] = "does not target {}, which `{}` names";
const uint8_t gm_ss3m7p8o4vdj0[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\252\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_ss3tptge0i2av[36] = "workspace/didChangeWorkspaceFolders";
const uint8_t gm_ss8lvrvkhracl[21] = " = (gm_node){.eff = ";
const uint8_t gm_ss8mnjves3ogt[12] = "lir_analyze";
const uint8_t gm_ss9fj8tanu5jn[51] = "` is not allowed in an untyped constant expression";
const uint8_t gm_ssbqeok4607ju[25] = ": \342\200\246)`, or with `fill:`";
const uint8_t gm_ssf1r8m922l8a[16] = "(field: value)`";
const uint8_t gm_ssgnvuivrkqi2[84] = "`continue` finishes the clause with `()`, but the operation returns `{}` (\302\24712.7.1)";
const uint8_t gm_ssj9o3d4o7kbv[43] = "-byte pointers; only 4 and 8 are supported";
const uint8_t gm_ssn1v6q1cq4so[3] = " w";
const uint8_t gm_sso7iusdc4q0g[11] = "gm_pr{}_{}";
const uint8_t gm_sspppdgt1j7r3[29] = "cannot read GAMMA_RUNNER: {}";
const uint8_t gm_ssqg2h6hrn099[9] = "comptime";
const uint8_t gm_ssscpugolo2j1[37] = "bool {} = ({})({} >> (int){}) != {};";
const uint8_t gm_sstr4ffsnqfee[6] = " ^ 1)";
const uint8_t gm_ssvpi8gpvt9jm[10] = "gm_{}({})";
const uint8_t gm_st027n235d4us[45] = "`#fuel(n)` takes a positive integer constant";
const uint8_t gm_st0caaee167rh[24] = ";\012gm_fprop:\012    return ";
const uint8_t gm_st38p0j1i82np[11] = "res-unwrap";
const uint8_t gm_st3p5nb1e6cc8[5] = "core";
const uint8_t gm_st5oflrv0tmc6[21] = "__ORDER_BIG_ENDIAN__";
const uint8_t gm_st7pul9bg6mqu[8] = "no body";
const uint8_t gm_st943craifob3[31] = "duplicate tuple element name `";
const uint8_t gm_stapduvfaru1r[68] = "` and yields nothing), but the parameter takes an iterator (\302\24713.5)";
const uint8_t gm_stc3e0j0moc2h[5] = ") & ";
const uint8_t gm_stclkv4qnta30[9] = "env_args";
const uint8_t gm_std3g89n1j9ln[8] = "int16_t";
const uint8_t gm_std91rqsr8j0b[7] = " quote";
const uint8_t gm_stfor4qir1ijs[14] = "gm_heap_alloc";
const uint8_t gm_stgk573j9ijrq[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0009\003\000\000\012\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_stn0r86gl3sc3[13] = "  import {} ";
const uint8_t gm_stott1n61n0gf[23] = "Cannot allocate memory";
const uint8_t gm_stotuneqhj37e[36] = "` on a branch or tag (required by `";
const uint8_t gm_stpdu6olnph3l[33] = "the C back end cannot translate ";
const uint8_t gm_stpevuujkc7eh[113] = "a local holding a copy of a list, map or other value with a resizable buffer, changed as if it were the original";
const uint8_t gm_stpus1r2o4u5q[14] = "select_nth_by";
const uint8_t gm_stqiimqtsnd8d[9] = "union { ";
const uint8_t gm_sttp5u65omp7t[15] = "selectionRange";
const uint8_t gm_stvgh52bfcgit[52] = " (`!e` makes an error; logical negation is `not e`)";
const uint8_t gm_su0qghe21d3de[26] = "EOF while parsing a value";
const uint8_t gm_su2ndlglvt2c0[11] = ", fb->arg)";
const uint8_t gm_su2vudghakb3u[55] = "a `@write` sink is a `fn(bytes: []const u8)`, not `{}`";
const uint8_t gm_su30fl1m1ebg6[5] = "`.\077`";
const uint8_t gm_su42nkrseeged[18] = "` has no element ";
const uint8_t gm_su71itml70ikf[5] = "r->a";
const uint8_t gm_su7ft5rc766n4[3] = "eE";
const uint8_t gm_su857f8o8i6a0[13] = "(void *)&F.w";
const uint8_t gm_su8ulebf2ijla[20] = "__builtin_floor({})";
const uint8_t gm_sua89kt0f2v9n[30] = "struct {} {{\012    gm_frame h;\012";
const uint8_t gm_suatl193icom4[41] = "a function body after the `where` clause";
const uint8_t gm_suf3ai1bhvntt[8] = "gm_tmpl";
const uint8_t gm_sufbh0g4mbom0[11] = "end_thread";
const uint8_t gm_suftpl3n8736g[5] = "({})";
const uint8_t gm_suifusi0pb6s0[12] = "the label `";
const uint8_t gm_sujvknhnh5am8[19] = "an `if` expression";
const uint8_t gm_suketc630ddb9[61] = "a constant pattern needs an integer, `bool` or string, not `";
const uint8_t gm_sumd85rpgjjrv[9] = "{}tir {}";
const uint8_t gm_supi6mhbcvtna[5] = "git+";
const uint8_t gm_supobfk7sjp99[48] = "{}: source file is not valid UTF-8 (at byte {})";
const uint8_t gm_suvf0cqih9u7i[17] = "kind = GM_K_BOOL";
const uint8_t gm_suvpgtuh49tp7[20] = "({} < 0 \077 -{} : {})";
const uint8_t gm_sv7lsha1e0i8r[13] = "this builtin";
const uint8_t gm_sv8qamggba7e1[18] = "({})(({}){} & {})";
const uint8_t gm_sv9j89ufns35b[16] = "[dependencies]\012";
const uint8_t gm_svd1idu6ksvnb[90] = "    if (file != NULL) { const void *fp = file; uintptr_t fl = strlen(file); memcpy(loc + ";
const uint8_t gm_svg70al9ibq8e[56] = "`{}(field: value)` names exactly one field of the union";
const uint8_t gm_svhe917rela4j[20] = "` has none (\302\24716.8)";
const uint8_t gm_svksed7nkdegg[25] = "compiler/lsp/features.gm";
const uint8_t gm_svld8cc4ptomt[16] = " does not exist";
const uint8_t gm_svocs6aii9rqg[75] = "`{}` contains itself by value and would be infinitely large; use a pointer";
const uint8_t gm_svulttobk4q7t[4] = "a-w";
const uint8_t gm_svv63hse0pejb[18] = "construction of `";
const uint8_t gm_svvuql4te1jp4[9] = "ptr-type";
uint8_t gm_c021omeko26[8] __attribute__((aligned(1)));
uint8_t gm_c0nmfbvmuf3[21] __attribute__((aligned(1)));
uint8_t gm_c0uuouc2r1v[51] __attribute__((aligned(1)));
uint8_t gm_c12r28j4ti0[7] __attribute__((aligned(1)));
uint8_t gm_c1ud45er2tv[1] __attribute__((aligned(1)));
uint8_t gm_c291b4sguf9[11] __attribute__((aligned(1)));
uint8_t gm_c2dh2r1v1kg[35] __attribute__((aligned(1)));
uint8_t gm_c55hukqtjhj[3] __attribute__((aligned(1)));
uint8_t gm_c5efon5060b[11] __attribute__((aligned(1)));
uint8_t gm_c5hm1q1bhjb[3] __attribute__((aligned(1)));
uint8_t gm_c5mskvieaph[12] __attribute__((aligned(1)));
uint8_t gm_c69snm9duhi[16] __attribute__((aligned(8)));
uint8_t gm_c6l682gqq6b[11] __attribute__((aligned(1)));
uint8_t gm_c6rb5tij85l[11] __attribute__((aligned(1)));
uint8_t gm_c75ivhf79kk[12] __attribute__((aligned(1)));
uint8_t gm_c7de3eptq2i[7] __attribute__((aligned(1)));
uint8_t gm_c7oeondv7ro[15] __attribute__((aligned(1)));
uint8_t gm_c8fs1d0gfop[11] __attribute__((aligned(1)));
uint8_t gm_c8mhuortijc[14] __attribute__((aligned(1)));
uint8_t gm_ca5fsoiskc2[4160] __attribute__((aligned(8)));
uint8_t gm_capvqjk6u1v[2] __attribute__((aligned(1)));
uint8_t gm_cb66i48lh38[15] __attribute__((aligned(1)));
uint8_t gm_cbgiap4qvea[7] __attribute__((aligned(1)));
uint8_t gm_cbrgbb0abkn[87] __attribute__((aligned(1)));
uint8_t gm_cc5iio3s0t7[8] __attribute__((aligned(1)));
uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
uint8_t gm_cc8o9o6j033[1] __attribute__((aligned(1)));
uint8_t gm_celk2vfrrsg[5] __attribute__((aligned(1)));
uint8_t gm_celpgpm1fi8[1] __attribute__((aligned(1)));
uint8_t gm_cfmhli3rcvs[7] __attribute__((aligned(1)));
uint8_t gm_cg669l82cvi[9] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e_3[16] __attribute__((aligned(8)));
uint8_t gm_ch3qcmtapgq[1] __attribute__((aligned(1)));
uint8_t gm_chtf2n20g83[1] __attribute__((aligned(1)));
uint8_t gm_chv3533b78v[72] __attribute__((aligned(8)));
uint8_t gm_ckus03er382_3[4] __attribute__((aligned(4)));
uint8_t gm_cl1ove1i7cb[16] __attribute__((aligned(8)));
uint8_t gm_cm0gcp6hd1u[8] __attribute__((aligned(1)));
uint8_t gm_cmf6ku0sokq[14] __attribute__((aligned(1)));
uint8_t gm_cmj35tijhpe[7] __attribute__((aligned(1)));
uint8_t gm_cng4093cslh[14] __attribute__((aligned(1)));
uint8_t gm_cnjfl2vnhcn[10] __attribute__((aligned(1)));
uint8_t gm_cnst39lk6ll[77] __attribute__((aligned(1)));
uint8_t gm_coaen0t0mnr[13] __attribute__((aligned(1)));
uint8_t gm_cop8vf1t852[5] __attribute__((aligned(1)));
uint8_t gm_coqj6tssqes[9] __attribute__((aligned(1)));
uint8_t gm_cos4i51m3eh[4] __attribute__((aligned(1)));
uint8_t gm_cpkjrtfhmna[10] __attribute__((aligned(1)));
uint8_t gm_cpocp7udld7[1] __attribute__((aligned(1)));
uint8_t gm_cpr60jghihe[16] __attribute__((aligned(8)));
uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
uint8_t gm_cramav0v1j7[16] __attribute__((aligned(8)));
uint8_t gm_csshki16t34[4] __attribute__((aligned(4)));
uint8_t gm_cthdq3e6ori[7] __attribute__((aligned(1)));
uint8_t gm_ctq9itg1v7n[9] __attribute__((aligned(1)));
uint8_t gm_ctt52f5sga2[7] __attribute__((aligned(1)));
uint8_t gm_cuf2teap2n5[11] __attribute__((aligned(1)));
uint8_t gm_cumduqk6tqi[14] __attribute__((aligned(1)));
uint8_t gm_cuokidpd7vd[5] __attribute__((aligned(1)));
uint8_t gm_cv5npqnefda[6] __attribute__((aligned(1)));
uint8_t gm_cvf6snt0g1j[59] __attribute__((aligned(1)));
uint8_t gm_cvh4e3tm5ir[14] __attribute__((aligned(1)));
uint8_t gm_k1umcn50hea[40] __attribute__((aligned(8)));
uint8_t gm_k1umgjqrbkf[40] __attribute__((aligned(8)));
uint8_t gm_k5m9lemc4jn[40] __attribute__((aligned(8)));
uint8_t gm_k7td060pd86[40] __attribute__((aligned(8)));
uint8_t gm_k8majipqr8n[40] __attribute__((aligned(8)));
uint8_t gm_k8nmk7je8im[40] __attribute__((aligned(8)));
uint8_t gm_ka38s9qhgtd[40] __attribute__((aligned(8)));
uint8_t gm_ka5jrrkpb0m[40] __attribute__((aligned(8)));
uint8_t gm_kakc8s3pa9o[40] __attribute__((aligned(8)));
uint8_t gm_kbhckstt7he[40] __attribute__((aligned(8)));
uint8_t gm_kbnbrskseut[40] __attribute__((aligned(8)));
uint8_t gm_kf1avcjqd98[40] __attribute__((aligned(8)));
uint8_t gm_kgk1u1jppkr[40] __attribute__((aligned(8)));
uint8_t gm_kgubkej3sov[40] __attribute__((aligned(8)));
uint8_t gm_khov2p23dnf[40] __attribute__((aligned(8)));
uint8_t gm_khqbs9bi0vi[40] __attribute__((aligned(8)));
uint8_t gm_khsmm7kmq9o[40] __attribute__((aligned(8)));
uint8_t gm_kil3h1h1ecr[40] __attribute__((aligned(8)));
uint8_t gm_kj662j6koce[40] __attribute__((aligned(8)));
uint8_t gm_kn81j57eee2[40] __attribute__((aligned(8)));
uint8_t gm_kolm4drqk8i[40] __attribute__((aligned(8)));
uint8_t gm_kp8bqs68o7b[40] __attribute__((aligned(8)));
uint8_t gm_ktlk7p7kh6n[40] __attribute__((aligned(8)));
uint8_t gm_kuspe9g7ejq[40] __attribute__((aligned(8)));
void *const gm_hl0b9itmh1di[] = {(void *)gm_f_for_body_i2u6ls4};
void *const gm_hl5aqi4imlms[] = {(void *)gm_f_for_body_vgf3305};
void *const gm_hlhb9f1a7omo[] = {(void *)gm_f_for_body_1063u09};
void *const gm_hlklsb3fd3g1[] = {(void *)gm_f_for_body_qfqkllj};
void *const gm_hlpd8uohstag[] = {(void *)gm_f_for_body_g1u7f51};
void *const gm_hlqu6cf845ie[] = {(void *)gm_f_for_body_tbgr2dj};
void *const gm_hlrddq2io9s1[] = {(void *)gm_f_for_body_j11vulv};

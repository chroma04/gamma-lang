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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_tilqfqdpj446h;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t pe[4]; } gm_t0o0svomecjg6;
typedef struct { gm_t9805qtonbsag f0; void * f1; } gm_t12ib7gg43lh0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
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
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tilqfqdpj446h) == 12 && _Alignof(gm_tilqfqdpj446h) == 4 && offsetof(gm_tilqfqdpj446h, f0) == 0 && offsetof(gm_tilqfqdpj446h, f1) == 4 && offsetof(gm_tilqfqdpj446h, f2) == 8, "layout of `(dst: u32, f: u32, second: bool)`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; gm_t9805qtonbsag f3; } gm_tp4rm55bg84ue;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t p4[4]; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; uint32_t f7; uint8_t pe[4]; } gm_tqh25f25b69k5;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_trt0mvq732stt;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0o0svomecjg6) == 32 && _Alignof(gm_t0o0svomecjg6) == 8 && offsetof(gm_t0o0svomecjg6, f0) == 0 && offsetof(gm_t0o0svomecjg6, f1) == 8 && offsetof(gm_t0o0svomecjg6, f2) == 16 && offsetof(gm_t0o0svomecjg6, f3) == 24 && offsetof(gm_t0o0svomecjg6, f4) == 25 && offsetof(gm_t0o0svomecjg6, f5) == 26 && offsetof(gm_t0o0svomecjg6, f6) == 27, "layout of `Local`");
_Static_assert(sizeof(gm_t12ib7gg43lh0) == 16 && _Alignof(gm_t12ib7gg43lh0) == 8 && offsetof(gm_t12ib7gg43lh0, f0) == 0 && offsetof(gm_t12ib7gg43lh0, f1) == 8, "layout of `(dst: ?u32, expr: ^Expr)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?(e: ^const Expr, l: u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { gm_tp4rm55bg84ue f0; uint8_t f1; uint8_t pe[3]; } gm_t7isn7qnmqh8i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_te9hrk0u35rev v0; gm_te9hrk0u35rev v1; uint32_t v2; } f1; } gm_t7pnmbsgd9mnq;
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
typedef struct { gm_t9805qtonbsag f0; gm_t8588ma2cr9d7 f1; uint8_t p2[4]; gm_b16_8 f2; } gm_te4bkgqfi01mk;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; } gm_tig4i22alq15r;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t p6[4]; gm_trqfvmjtt4sij f6; uint32_t f7; uint8_t p8[4]; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_t5lf9i3n1r3co f10; uint8_t f11; uint8_t pe[7]; } gm_tl905bk085r8i;
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tp4rm55bg84ue) == 20 && _Alignof(gm_tp4rm55bg84ue) == 4 && offsetof(gm_tp4rm55bg84ue, f0) == 0 && offsetof(gm_tp4rm55bg84ue, f1) == 4 && offsetof(gm_tp4rm55bg84ue, f2) == 8 && offsetof(gm_tp4rm55bg84ue, f3) == 12, "layout of `LoopLabels`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_tqh25f25b69k5) == 96 && _Alignof(gm_tqh25f25b69k5) == 8 && offsetof(gm_tqh25f25b69k5, f0) == 0 && offsetof(gm_tqh25f25b69k5, f1) == 8 && offsetof(gm_tqh25f25b69k5, f2) == 24 && offsetof(gm_tqh25f25b69k5, f3) == 40 && offsetof(gm_tqh25f25b69k5, f4) == 48 && offsetof(gm_tqh25f25b69k5, f5) == 64 && offsetof(gm_tqh25f25b69k5, f6) == 72 && offsetof(gm_tqh25f25b69k5, f7) == 88, "layout of `Func`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_trt0mvq732stt) == 32 && _Alignof(gm_trt0mvq732stt) == 8 && offsetof(gm_trt0mvq732stt, f0) == 0 && offsetof(gm_trt0mvq732stt, f1) == 8 && offsetof(gm_trt0mvq732stt, f2) == 24, "layout of `(dst: u32, bytes: string, c: bool)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
typedef struct { uint32_t f0; gm_ts6oqppjh0ba3 f1; uint8_t p2[10]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_t1ctgp8j1v2as;
typedef struct { gm_t7b7esi3n9r90 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t1s3s9ds8udn4;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t7b7esi3n9r90 f0; uint64_t f1; uint8_t pe[8]; } gm_t592joge1j4uv;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
typedef struct { uint32_t f0; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; gm_t7b7esi3n9r90 f2; } gm_t5hdm76ui2uro;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t7isn7qnmqh8i) == 24 && _Alignof(gm_t7isn7qnmqh8i) == 4 && offsetof(gm_t7isn7qnmqh8i, f0) == 0 && offsetof(gm_t7isn7qnmqh8i, f1) == 20, "layout of `?LoopLabels`");
_Static_assert(sizeof(gm_t7pnmbsgd9mnq) == 16 && _Alignof(gm_t7pnmbsgd9mnq) == 4 && offsetof(gm_t7pnmbsgd9mnq, f0) == 0 && offsetof(gm_t7pnmbsgd9mnq, f1) == 4, "layout of `Exit`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t p3[12]; gm_t7b7esi3n9r90 f3; } gm_t8rrrcunvsibs;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; } gm_tbto4k5jg9fve;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t pe[12]; } gm_tcpisgbff1dqv;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_te4bkgqfi01mk) == 40 && _Alignof(gm_te4bkgqfi01mk) == 8 && offsetof(gm_te4bkgqfi01mk, f0) == 0 && offsetof(gm_te4bkgqfi01mk, f1) == 8 && offsetof(gm_te4bkgqfi01mk, f2) == 24, "layout of `(dst: ?u32, op: Builtin, args: []Value)`");
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint8_t pe[8]; } gm_tejc6vv5sqno9;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; uint8_t f4; uint8_t p5[7]; uint64_t f5; } gm_tgroihbjsmdvm;
_Static_assert(sizeof(gm_tig4i22alq15r) == 48 && _Alignof(gm_tig4i22alq15r) == 8 && offsetof(gm_tig4i22alq15r, f0) == 0 && offsetof(gm_tig4i22alq15r, f1) == 8, "layout of `Ctx`");
_Static_assert(sizeof(gm_tl905bk085r8i) == 272 && _Alignof(gm_tl905bk085r8i) == 8 && offsetof(gm_tl905bk085r8i, f0) == 0 && offsetof(gm_tl905bk085r8i, f1) == 8 && offsetof(gm_tl905bk085r8i, f2) == 16 && offsetof(gm_tl905bk085r8i, f3) == 56 && offsetof(gm_tl905bk085r8i, f4) == 96 && offsetof(gm_tl905bk085r8i, f5) == 136 && offsetof(gm_tl905bk085r8i, f6) == 144 && offsetof(gm_tl905bk085r8i, f7) == 184 && offsetof(gm_tl905bk085r8i, f8) == 192 && offsetof(gm_tl905bk085r8i, f9) == 232 && offsetof(gm_tl905bk085r8i, f10) == 240 && offsetof(gm_tl905bk085r8i, f11) == 264, "layout of `Lw`");
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint32_t f0; uint8_t f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_tr6ll1dg9imrl;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
typedef struct { gm_t9805qtonbsag f0; uint8_t p1[8]; gm_tllthq21h2f0k f1; gm_b16_8 f2; } gm_t116nudii5c54;
_Static_assert(sizeof(gm_t1ctgp8j1v2as) == 112 && _Alignof(gm_t1ctgp8j1v2as) == 16 && offsetof(gm_t1ctgp8j1v2as, f0) == 0 && offsetof(gm_t1ctgp8j1v2as, f1) == 4 && offsetof(gm_t1ctgp8j1v2as, f2) == 16 && offsetof(gm_t1ctgp8j1v2as, f3) == 64, "layout of `(dst: u32, op: BinOp, a: Value, b: Value)`");
_Static_assert(sizeof(gm_t1s3s9ds8udn4) == 80 && _Alignof(gm_t1s3s9ds8udn4) == 16 && offsetof(gm_t1s3s9ds8udn4, f0) == 0 && offsetof(gm_t1s3s9ds8udn4, f1) == 48 && offsetof(gm_t1s3s9ds8udn4, f2) == 64, "layout of `(cond: Value, then: []Inst, else_: []Inst)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t3349g713hmcr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint32_t f2; uint8_t pe[4]; } gm_t4j77n1ssj077;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t592joge1j4uv) == 64 && _Alignof(gm_t592joge1j4uv) == 16 && offsetof(gm_t592joge1j4uv, f0) == 0 && offsetof(gm_t592joge1j4uv, f1) == 48, "layout of `(index: Value, size: u64)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_t5hdm76ui2uro) == 64 && _Alignof(gm_t5hdm76ui2uro) == 16 && offsetof(gm_t5hdm76ui2uro, f0) == 0 && offsetof(gm_t5hdm76ui2uro, f1) == 4 && offsetof(gm_t5hdm76ui2uro, f2) == 16, "layout of `(dst: u32, kind: Coercion, v: Value)`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t p1[8]; gm_t7b7esi3n9r90 f1; } gm_t6fp3q71vjrmk;
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
typedef struct { gm_t7pnmbsgd9mnq f0; gm_tf8kfhcnval78 f1; } gm_t7dcjsiqi95h3;
_Static_assert(sizeof(gm_t8rrrcunvsibs) == 128 && _Alignof(gm_t8rrrcunvsibs) == 16 && offsetof(gm_t8rrrcunvsibs, f0) == 0 && offsetof(gm_t8rrrcunvsibs, f1) == 16 && offsetof(gm_t8rrrcunvsibs, f2) == 64 && offsetof(gm_t8rrrcunvsibs, f3) == 80, "layout of `(dst: u32, whole: Value, index: u32, v: Value)`");
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tbto4k5jg9fve) == 64 && _Alignof(gm_tbto4k5jg9fve) == 16 && offsetof(gm_tbto4k5jg9fve, f0) == 0 && offsetof(gm_tbto4k5jg9fve, f1) == 4 && offsetof(gm_tbto4k5jg9fve, f2) == 16, "layout of `(dst: u32, op: UnOp, a: Value)`");
_Static_assert(sizeof(gm_tcpisgbff1dqv) == 80 && _Alignof(gm_tcpisgbff1dqv) == 16 && offsetof(gm_tcpisgbff1dqv, f0) == 0 && offsetof(gm_tcpisgbff1dqv, f1) == 16 && offsetof(gm_tcpisgbff1dqv, f2) == 64, "layout of `(dst: u32, v: Value, index: u32)`");
_Static_assert(sizeof(gm_tejc6vv5sqno9) == 64 && _Alignof(gm_tejc6vv5sqno9) == 16 && offsetof(gm_tejc6vv5sqno9, f0) == 0 && offsetof(gm_tejc6vv5sqno9, f1) == 48 && offsetof(gm_tejc6vv5sqno9, f2) == 52, "layout of `(cond: Value, negated: bool, label: u32)`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t f1; uint8_t pe[7]; } gm_tfonjjajjj3jq;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tb31k5l794t7p v0; gm_t592joge1j4uv v1; gm_t592joge1j4uv v2; gm_tlcm4lvu3g1a8 v3; } f1; } gm_tfqmtuprvunod;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_tf8kfhcnval78 f2; } gm_tg85uffmq8b38;
_Static_assert(sizeof(gm_tgroihbjsmdvm) == 176 && _Alignof(gm_tgroihbjsmdvm) == 16 && offsetof(gm_tgroihbjsmdvm, f0) == 0 && offsetof(gm_tgroihbjsmdvm, f1) == 16 && offsetof(gm_tgroihbjsmdvm, f2) == 64 && offsetof(gm_tgroihbjsmdvm, f3) == 112 && offsetof(gm_tgroihbjsmdvm, f4) == 160 && offsetof(gm_tgroihbjsmdvm, f5) == 168, "layout of `(dst: u32, ptr: Value, lo: Value, hi: Value, inclusive: bool, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { gm_t9i04n6j2d1gi f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_tf8kfhcnval78 f4; } gm_tihh2qes0hpd4;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; } gm_tlpcrb3vu6o2d;
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tr6ll1dg9imrl) == 112 && _Alignof(gm_tr6ll1dg9imrl) == 16 && offsetof(gm_tr6ll1dg9imrl, f0) == 0 && offsetof(gm_tr6ll1dg9imrl, f1) == 4 && offsetof(gm_tr6ll1dg9imrl, f2) == 16 && offsetof(gm_tr6ll1dg9imrl, f3) == 64, "layout of `(dst: u32, negated: bool, a: Value, b: Value)`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; uint8_t p3[8]; gm_tf8kfhcnval78 f3; gm_b16_8 f4; } gm_tvdggnhat0k99;
typedef struct { uint32_t f0; uint32_t f1; uint8_t p2[8]; gm_tf8kfhcnval78 f2; } gm_tvv8lnu6a3ppj;
_Static_assert(sizeof(gm_t116nudii5c54) == 96 && _Alignof(gm_t116nudii5c54) == 16 && offsetof(gm_t116nudii5c54, f0) == 0 && offsetof(gm_t116nudii5c54, f1) == 16 && offsetof(gm_t116nudii5c54, f2) == 80, "layout of `(dst: ?u32, callee: Callee, args: []Value)`");
typedef struct { uint64_t f0; gm_tfonjjajjj3jq f1; } gm_t30kh7ksmrtsb;
_Static_assert(sizeof(gm_t3349g713hmcr) == 72 && _Alignof(gm_t3349g713hmcr) == 8 && offsetof(gm_t3349g713hmcr, f0) == 0 && offsetof(gm_t3349g713hmcr, f1) == 8 && offsetof(gm_t3349g713hmcr, f2) == 64, "layout of `(dst: u32, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t4j77n1ssj077) == 72 && _Alignof(gm_t4j77n1ssj077) == 8 && offsetof(gm_t4j77n1ssj077, f0) == 0 && offsetof(gm_t4j77n1ssj077, f1) == 8 && offsetof(gm_t4j77n1ssj077, f2) == 64, "layout of `(dst: u32, place: Place, variant: u32)`");
_Static_assert(sizeof(gm_t6fp3q71vjrmk) == 112 && _Alignof(gm_t6fp3q71vjrmk) == 16 && offsetof(gm_t6fp3q71vjrmk, f0) == 0 && offsetof(gm_t6fp3q71vjrmk, f1) == 64, "layout of `(place: Place, v: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t7dcjsiqi95h3) == 80 && _Alignof(gm_t7dcjsiqi95h3) == 16 && offsetof(gm_t7dcjsiqi95h3, f0) == 0 && offsetof(gm_t7dcjsiqi95h3, f1) == 16, "layout of `(kind: Exit, v: ?Value)`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
_Static_assert(sizeof(gm_tfonjjajjj3jq) == 64 && _Alignof(gm_tfonjjajjj3jq) == 8 && offsetof(gm_tfonjjajjj3jq, f0) == 0 && offsetof(gm_tfonjjajjj3jq, f1) == 56, "layout of `?Place`");
_Static_assert(sizeof(gm_tfqmtuprvunod) == 80 && _Alignof(gm_tfqmtuprvunod) == 16 && offsetof(gm_tfqmtuprvunod, f0) == 0 && offsetof(gm_tfqmtuprvunod, f1) == 16, "layout of `Proj`");
_Static_assert(sizeof(gm_tg85uffmq8b38) == 80 && _Alignof(gm_tg85uffmq8b38) == 16 && offsetof(gm_tg85uffmq8b38, f0) == 0 && offsetof(gm_tg85uffmq8b38, f1) == 4 && offsetof(gm_tg85uffmq8b38, f2) == 16, "layout of `(dst: u32, kind: Wrap, v: ?Value)`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tihh2qes0hpd4) == 176 && _Alignof(gm_tihh2qes0hpd4) == 16 && offsetof(gm_tihh2qes0hpd4, f0) == 0 && offsetof(gm_tihh2qes0hpd4, f1) == 64 && offsetof(gm_tihh2qes0hpd4, f2) == 80 && offsetof(gm_tihh2qes0hpd4, f3) == 96 && offsetof(gm_tihh2qes0hpd4, f4) == 112, "layout of `(sink: Sink, pieces: []const string, specs: []const FmtSpec, args: []Value, table: ?Value)`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tlpcrb3vu6o2d) == 64 && _Alignof(gm_tlpcrb3vu6o2d) == 8 && offsetof(gm_tlpcrb3vu6o2d, f0) == 0 && offsetof(gm_tlpcrb3vu6o2d, f1) == 8, "layout of `(dst: u32, place: Place)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tvdggnhat0k99) == 112 && _Alignof(gm_tvdggnhat0k99) == 16 && offsetof(gm_tvdggnhat0k99, f0) == 0 && offsetof(gm_tvdggnhat0k99, f1) == 4 && offsetof(gm_tvdggnhat0k99, f2) == 8 && offsetof(gm_tvdggnhat0k99, f3) == 32 && offsetof(gm_tvdggnhat0k99, f4) == 96, "layout of `(label: u32, tir_label: u32, install: []Inst, on: ?Value, body: []Inst)`");
_Static_assert(sizeof(gm_tvv8lnu6a3ppj) == 80 && _Alignof(gm_tvv8lnu6a3ppj) == 16 && offsetof(gm_tvv8lnu6a3ppj, f0) == 0 && offsetof(gm_tvv8lnu6a3ppj, f1) == 4 && offsetof(gm_tvv8lnu6a3ppj, f2) == 16, "layout of `(dst: u32, variant: u32, payload: ?Value)`");
_Static_assert(sizeof(gm_t30kh7ksmrtsb) == 72 && _Alignof(gm_t30kh7ksmrtsb) == 8 && offsetof(gm_t30kh7ksmrtsb, f0) == 0 && offsetof(gm_t30kh7ksmrtsb, f1) == 8, "layout of `(i: usize, x: ?Place)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tf4lk4crue8vt v0; gm_tlpcrb3vu6o2d v1; gm_tlpcrb3vu6o2d v2; gm_t1ctgp8j1v2as v3; gm_tbto4k5jg9fve v4; gm_tr6ll1dg9imrl v5; gm_t5hdm76ui2uro v6; gm_te4bkgqfi01mk v7; gm_t116nudii5c54 v8; gm_tiilmfd16v3fc v9; gm_trt0mvq732stt v10; gm_tf4lk4crue8vt v11; gm_tgroihbjsmdvm v12; gm_tihh2qes0hpd4 v13; gm_tvv8lnu6a3ppj v14; gm_tg85uffmq8b38 v15; gm_tilqfqdpj446h v16; gm_tcc1rq4n1avb2 v17; gm_tcc1rq4n1avb2 v18; gm_ttggq7ng4kp3p v19; gm_tr6ll1dg9imrl v20; gm_tcpisgbff1dqv v21; gm_t8rrrcunvsibs v22; gm_t3349g713hmcr v23; gm_t4j77n1ssj077 v24; gm_t6fp3q71vjrmk v25; gm_tkj0hg83g38nr v26; gm_tiilmfd16v3fc v27; gm_tiilmfd16v3fc v28; gm_t1s3s9ds8udn4 v29; uint32_t v30; gm_tejc6vv5sqno9 v31; gm_tf8kfhcnval78 v32; gm_thervm8j23qet v33; gm_t7b7esi3n9r90 v34; gm_tf8kfhcnval78 v35; gm_b16_8 v36; gm_tvdggnhat0k99 v37; gm_tkgci6o4gktbf v38; gm_t7dcjsiqi95h3 v39; gm_tthvfhsvm2g0s v40; gm_t7b7esi3n9r90 v41; gm_t12ib7gg43lh0 v42; } f1; } gm_t8658kcqovtpe;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t8658kcqovtpe) == 208 && _Alignof(gm_t8658kcqovtpe) == 16 && offsetof(gm_t8658kcqovtpe, f0) == 0 && offsetof(gm_t8658kcqovtpe, f1) == 16, "layout of `Op`");
typedef struct { gm_t8658kcqovtpe f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t8g064jsjmhum;
_Static_assert(sizeof(gm_t8g064jsjmhum) == 224 && _Alignof(gm_t8g064jsjmhum) == 16 && offsetof(gm_t8g064jsjmhum, f0) == 0 && offsetof(gm_t8g064jsjmhum, f1) == 208, "layout of `Inst`");
typedef struct { gm_t8g064jsjmhum f0; uint8_t f1; uint8_t pe[15]; } gm_tmpdn4irouh8b;
_Static_assert(sizeof(gm_tmpdn4irouh8b) == 240 && _Alignof(gm_tmpdn4irouh8b) == 16 && offsetof(gm_tmpdn4irouh8b, f0) == 0 && offsetof(gm_tmpdn4irouh8b, f1) == 224, "layout of `?Inst`");
struct gm_fr_assert__nol4kp3 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_b16_8 l2; /* args */
    gm_b0_1 ret;
};
struct gm_fr_bits_field_cdqu5on {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_block_iujbbjb {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* b */
    gm_t9805qtonbsag l9; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_bounds_6f9s0q1 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* index */
    gm_t7b7esi3n9r90 l2; /* len */
    gm_b0_1 ret;
};
struct gm_fr_conds_sqc7llk {
    gm_frame h;
    gm_t7b7esi3n9r90 l7; /* v */
    gm_t7b7esi3n9r90 l10; /* v */
    gm_tlg23kb3oj23s l12; /* p */
    gm_b0_1 ret;
};
struct gm_fr_context_l4fgtfp {
    gm_frame h;
    gm_tig4i22alq15r l1; /* cx */
    gm_tig4i22alq15r ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_diverges_p3okmol {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_else_expr_lrum9ir {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t3dcoc7077t1q l2; /* x */
    void * l3; /* s */
    gm_t88rcucg1b7uk l5; /* inner */
    gm_t9805qtonbsag l6; /* res */
    gm_tlg23kb3oj23s l7; /* p */
    uint64_t l9; /* off */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_emit_q94msgt {
    gm_frame h;
    gm_t8658kcqovtpe l1; /* op */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4g9am13 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6h1imen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d1l4euh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fooqdot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hh33a3d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_i0ticdn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uvn29bg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escape_avsr9t6 {
    gm_frame h;
    void * l0; /* lw */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_escape_pattern_6lpj7tc {
    gm_frame h;
    gm_tlg23kb3oj23s l2; /* p */
    gm_t7b7esi3n9r90 l5; /* v */
    gm_trqfvmjtt4sij l8; /* arms */
    gm_t7b7esi3n9r90 l11; /* matched */
    gm_b0_1 ret;
};
struct gm_fr_escape_stmt_g02tr5c {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    gm_b0_1 ret;
};
struct gm_fr_expr_tp86bm9 {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* i */
    gm_t7b7esi3n9r90 l9; /* v */
    gm_tlg23kb3oj23s l15; /* p */
    gm_t7b7esi3n9r90 l21; /* v */
    gm_t7b7esi3n9r90 l27; /* a */
    gm_t7b7esi3n9r90 l28; /* b */
    gm_t7b7esi3n9r90 l32; /* a */
    gm_t7b7esi3n9r90 l33; /* b */
    gm_t7b7esi3n9r90 l35; /* a */
    gm_t710lsrq1m8gk l36; /* w */
    gm_t9i04n6j2d1gi l41; /* sink */
    gm_tf8kfhcnval78 l42; /* table */
    gm_t7b7esi3n9r90 l47; /* w */
    gm_t7b7esi3n9r90 l51; /* whole */
    gm_tlg23kb3oj23s l53; /* p */
    gm_tlg23kb3oj23s l55; /* p */
    gm_tlg23kb3oj23s l57; /* p */
    gm_tlg23kb3oj23s l59; /* p */
    gm_t7b7esi3n9r90 l69; /* a */
    gm_t7b7esi3n9r90 l74; /* a */
    gm_t7b7esi3n9r90 l75; /* b */
    gm_t7b7esi3n9r90 l80; /* a */
    gm_t7b7esi3n9r90 l81; /* b */
    gm_t7b7esi3n9r90 l88; /* v */
    gm_tsth9qu1dv0g3 l94; /* callee */
    gm_tllthq21h2f0k l100; /* c */
    gm_tf8kfhcnval78 l108; /* payload */
    gm_t7b7esi3n9r90 l112; /* v */
    gm_t710lsrq1m8gk l116; /* x */
    gm_tf8kfhcnval78 l118; /* v */
    gm_t7b7esi3n9r90 l120; /* val */
    gm_t7b7esi3n9r90 l124; /* val */
    gm_t7b7esi3n9r90 l130; /* v */
    gm_tf8kfhcnval78 l131; /* val */
    gm_t7b7esi3n9r90 l133; /* v */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_exprs_r7m130e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_extend_vbdqifj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_float_value_o4b87lc {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_fn_05n2pkm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_24nvriq {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* c */
    gm_b0_1 ret;
};
struct gm_fr_fn_36v8itm {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* w */
    gm_b0_1 ret;
};
struct gm_fr_fn_3sp5ule {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_47srmke {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_49psp5f {
    gm_frame h;
    gm_tf8kfhcnval78 l0; /* msg */
    gm_b0_1 ret;
};
struct gm_fr_fn_4lhsreh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4oh4m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_67nerak {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_74evcu5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7h6ducr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a8eu0io {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cccid3f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cquk74j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dduu5o8 {
    gm_frame h;
    void * l2; /* q */
    uint32_t l3; /* next */
    gm_b0_1 ret;
};
struct gm_fr_fn_drdvs2i {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* b */
    gm_b0_1 ret;
};
struct gm_fr_fn_etfgial {
    gm_frame h;
    gm_tbbqefuhv2r8h l2; /* arm */
    uint32_t l3; /* fail */
    gm_b0_1 ret;
};
struct gm_fr_fn_k0ofpte {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* w */
    gm_t7b7esi3n9r90 l3; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_k3pfo3n {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_m7prcu1 {
    gm_frame h;
    gm_tlg23kb3oj23s l1; /* at */
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_mh72upo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mo8l3p0 {
    gm_frame h;
    gm_t7b7esi3n9r90 l5; /* v */
    gm_t7b7esi3n9r90 l10; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_pmnv16v {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_rihlhf9 {
    gm_frame h;
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_roenhir {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v8tau4j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vkfbupa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_d0bmskj {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t8588ma2cr9d7 l2; /* op */
    void * l4; /* s */
    gm_t88rcucg1b7uk l6; /* inner */
    gm_t88rcucg1b7uk l7; /* elem */
    uint64_t l9; /* esz */
    gm_tlg23kb3oj23s l11; /* seq */
    gm_t9805qtonbsag l12; /* ptr */
    gm_t7b7esi3n9r90 l13; /* len */
    gm_tlg23kb3oj23s l17; /* p */
    gm_t7b7esi3n9r90 l19; /* i */
    uint32_t l21; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_if_expr_bsdi44h {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_trl2khmuphncd l2; /* x */
    gm_t9805qtonbsag l3; /* res */
    gm_t7b7esi3n9r90 l5; /* cv */
    uint32_t l8; /* end */
    uint32_t l9; /* els */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_in_temp_t7gs0ds {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    uint32_t ret;
};
struct gm_fr_index_place_3gfu5dn {
    gm_frame h;
    gm_tlg23kb3oj23s l9; /* p */
    gm_t7b7esi3n9r90 l10; /* i */
    gm_tlg23kb3oj23s l13; /* p */
    gm_t7b7esi3n9r90 l14; /* ptr */
    gm_t7b7esi3n9r90 l15; /* len */
    gm_t7b7esi3n9r90 l16; /* i */
    gm_t7b7esi3n9r90 l18; /* ptr */
    gm_t7b7esi3n9r90 l19; /* i */
    gm_t7b7esi3n9r90 l22; /* ptr */
    gm_t7b7esi3n9r90 l23; /* i */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_int_bits_6bio9a8 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_int_value_ttu5547 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* i */
    gm_tf8kfhcnval78 ret;
};
struct gm_fr_is_bits_pbijkuf {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    uint8_t ret;
};
struct gm_fr_is_place_expr_6llket3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_3373lil {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_leave_ehrof6p {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* v */
    gm_b0_1 ret;
};
struct gm_fr_list_0202sdh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0ngbo25 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a3ut5mo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gaq8h4q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hft9rao {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mgqfh8v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rb5mlsd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_urufktd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_go4fgkp {
    gm_frame h;
    gm_tlg23kb3oj23s l1; /* p */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_local_place_qnf3mb2 {
    gm_frame h;
    gm_tlg23kb3oj23s ret;
};
struct gm_fr_local_target_vvq8lnf {
    gm_frame h;
    gm_t7isn7qnmqh8i ret;
};
struct gm_fr_logic_9o6bp90 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    uint8_t l2; /* is_and */
    void * l4; /* r */
    uint32_t l5; /* res */
    gm_t7b7esi3n9r90 l6; /* a */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_loop_expr_k6v92f2 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t710lsrq1m8gk l2; /* x */
    uint32_t l4; /* brk */
    uint32_t l5; /* top */
    uint32_t l6; /* next_ */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_loop_labels_uvqimfl {
    gm_frame h;
    gm_t7isn7qnmqh8i ret;
};
struct gm_fr_lower_rh7s5j6 {
    gm_frame h;
    gm_tdgfduqjheqdm l3; /* def */
    gm_tl905bk085r8i l4; /* lw */
    gm_tsoie1ppgdvcg l8; /* l */
    gm_t7b7esi3n9r90 l12; /* v */
    gm_tqh25f25b69k5 l13; /* func */
    gm_tqh25f25b69k5 ret;
};
struct gm_fr_mark_addressed_t8qojin {
    gm_frame h;
    gm_tfqmtuprvunod l5; /* x */
    gm_b0_1 ret;
};
struct gm_fr_mark_captured_lpp77bm {
    gm_frame h;
    uint32_t l2; /* depth */
    gm_b16_8 l4; /* locals */
    gm_b0_1 ret;
};
struct gm_fr_mark_root_b973vvi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_match_expr_rfas5u2 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_b16_8 l3; /* arms */
    gm_t9805qtonbsag l4; /* res */
    gm_tlg23kb3oj23s l5; /* p */
    uint32_t l6; /* end */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_n667r26 {
    gm_frame h;
    gm_tfqmtuprvunod l0; /* x */
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_operand_place_cdle4ls {
    gm_frame h;
    gm_t7b7esi3n9r90 l2; /* v */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_pattern_od1h9r0 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* pat */
    gm_tlg23kb3oj23s l2; /* p */
    uint32_t l3; /* fail */
    gm_t7b7esi3n9r90 l7; /* v */
    gm_t7b7esi3n9r90 l9; /* v */
    gm_t7b7esi3n9r90 l10; /* k */
    gm_tbci5ospk5nnn l12; /* lo */
    gm_tbci5ospk5nnn l13; /* hi */
    gm_t7b7esi3n9r90 l15; /* v */
    gm_t7b7esi3n9r90 l16; /* a */
    gm_t7b7esi3n9r90 l17; /* b */
    gm_t7b7esi3n9r90 l31; /* whole */
    gm_b16_8 l42; /* alts */
    uint32_t l43; /* ok */
    gm_b0_1 ret;
};
struct gm_fr_payload_off_o6q9s7n {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_payload_ty_gn5u4tt {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_place_oohr45n {
    gm_frame h;
    gm_t7b7esi3n9r90 l10; /* p */
    gm_tlg23kb3oj23s l13; /* p */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_pop_2qgfbfe {
    gm_frame h;
    gm_t8g064jsjmhum l2; /* x */
    gm_tmpdn4irouh8b ret;
};
struct gm_fr_project_6hpd48e {
    gm_frame h;
    gm_tlg23kb3oj23s l0; /* p */
    gm_tfqmtuprvunod l1; /* x */
    gm_trqfvmjtt4sij l2; /* proj */
    gm_tlg23kb3oj23s ret;
};
struct gm_fr_ptr_size_lig81gu {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_0pgacsj {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2v345ri {
    gm_frame h;
    gm_tfonjjajjj3jq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_e2qt8as {
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
struct gm_fr_push_kki5o6e {
    gm_frame h;
    gm_tfqmtuprvunod l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ql89qc9 {
    gm_frame h;
    gm_t8g064jsjmhum l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r4haklv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u46i39f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u8i76or {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_result_8csahoh {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_region_q7ivn7p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* saved */
    gm_b16_8 ret;
};
struct gm_fr_regrow_0s9hfc3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5hl8984 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5l0p1mi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q7kpg21 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s8usk1j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tsagih4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_uue84k4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_result_for_6fafafc {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_result_local_pp1s755 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_result_of_9m0s9vd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_slice_dup5j25 {
    gm_frame h;
    gm_t7b7esi3n9r90 l7; /* ptr */
    gm_tf8kfhcnval78 l8; /* len */
    gm_tlg23kb3oj23s l10; /* p */
    gm_tlg23kb3oj23s l13; /* p */
    gm_t7b7esi3n9r90 l16; /* lo */
    gm_t7b7esi3n9r90 l18; /* hi */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_stmt_k90qtjr {
    gm_frame h;
    void * l0; /* lw */
    gm_t7b7esi3n9r90 l5; /* v */
    gm_tlg23kb3oj23s l9; /* p */
    gm_tlg23kb3oj23s l12; /* p */
    gm_t7b7esi3n9r90 l20; /* v */
    gm_trqfvmjtt4sij l23; /* chain */
    gm_tlg23kb3oj23s l26; /* bp */
    gm_t7b7esi3n9r90 l27; /* v */
    gm_trqfvmjtt4sij l28; /* wholes */
    gm_t7b7esi3n9r90 l32; /* nv */
    gm_tlg23kb3oj23s l39; /* p */
    gm_t7b7esi3n9r90 l40; /* v */
    gm_trqfvmjtt4sij l43; /* ps */
    gm_t7b7esi3n9r90 l48; /* v */
    gm_tfonjjajjj3jq l55; /* x */
    gm_tlg23kb3oj23s l56; /* q */
    gm_t7b7esi3n9r90 l57; /* f */
    void * l60; /* body */
    gm_b0_1 ret;
};
struct gm_fr_stmts_ctl06qt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_store_result_fiq5f1j {
    gm_frame h;
    gm_t7b7esi3n9r90 l3; /* v */
    gm_b0_1 ret;
};
struct gm_fr_synthetic_t3b2kd0 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_temp_lneuo24 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_try_expr_2gjs5f7 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t083kd7b1kkag l2; /* x */
    void * l3; /* s */
    gm_t88rcucg1b7uk l5; /* ret */
    uint8_t l6; /* is_result */
    gm_tlg23kb3oj23s l7; /* p */
    uint64_t l9; /* off */
    gm_closure l10; /* leave */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ggopu3h {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visit_ajtfk2n {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_with_expr_g16bcnu {
    gm_frame h;
    void * l0; /* lw */
    gm_t710lsrq1m8gk l2; /* w */
    void * l3; /* s */
    gm_tf8kfhcnval78 l4; /* on */
    gm_t9805qtonbsag l6; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_word_1sfojus {
    gm_frame h;
    gm_tf8kfhcnval78 ret;
};
struct gm_fr_zero_sln4lkh {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_ancestor_o28bfm6(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_block_iujbbjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bounds_6f9s0q1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_conds_sqc7llk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_context_l4fgtfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1);
__attribute__((aligned(4))) uint32_t gm_f_elide_checks_s9bgk65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_else_expr_lrum9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4g9am13(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d1l4euh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fooqdot(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uvn29bg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) void gm_f_escape_avsr9t6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_pattern_6lpj7tc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_stmt_g02tr5c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_expr_tp86bm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_exprs_r7m130e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vbdqifj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint16_t gm_f_f16_bits_2us9irg(double a0);
__attribute__((aligned(4))) void gm_f_float_value_o4b87lc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, double a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_05n2pkm(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_24nvriq(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_36v8itm(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sp5ule(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_49psp5f(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_4lhsreh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4oh4m1v(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_67nerak(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74evcu5(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a8eu0io(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_cccid3f(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_cquk74j(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_dduu5o8(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_drdvs2i(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_etfgial(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0ofpte(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k3pfo3n(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_m7prcu1(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mh72upo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo8l3p0(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pmnv16v(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_rihlhf9(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_v8tau4j(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vkfbupa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_d0bmskj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_if_expr_bsdi44h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) uint32_t gm_f_in_temp_t7gs0ds(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_index_place_3gfu5dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_int_value_ttu5547(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_bits_pbijkuf(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_place_expr_6llket3(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_leave_ehrof6p(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0ngbo25(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a3ut5mo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gaq8h4q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hft9rao(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_go4fgkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0);
__attribute__((aligned(4))) gm_tlg23kb3oj23s gm_f_local_place_qnf3mb2_c(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_local_target_vvq8lnf_c(void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) void gm_f_logic_9o6bp90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_loop_expr_k6v92f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_loop_labels_uvqimfl(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_loop_labels_uvqimfl_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_lower_rh7s5j6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_mark_addressed_t8qojin(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_mark_captured_lpp77bm(void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_mark_root_b973vvi(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_match_expr_rfas5u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_operand_place_cdle4ls(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_od1h9r0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3);
__attribute__((aligned(4))) uint64_t gm_f_payload_off_o6q9s7n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_gn5u4tt(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_place_oohr45n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_2qgfbfe(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_project_6hpd48e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_e2qt8as(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kki5o6e(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ql89qc9(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u8i76or(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0s9hfc3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5hl8984(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5l0p1mi(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tsagih4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_uue84k4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_for_6fafafc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_local_pp1s755(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_40frg7f(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_slice_dup5j25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_k90qtjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmts_ctl06qt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_store_result_fiq5f1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_synthetic_t3b2kd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_try_expr_2gjs5f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_with_expr_g16bcnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_word_1sfojus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0);
__attribute__((aligned(4))) gm_t7b7esi3n9r90 gm_f_zero_sln4lkh_c(gm_csguankko4lmhd a0);
extern uint8_t gm_cp0utnbgndu[16] __attribute__((aligned(8)));
extern uint8_t gm_k4iadrd58qb[40] __attribute__((aligned(8)));
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srccujoh2vgr9aq[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_assert__nol4kp3 F;
    gm_b16_8 gm_l3; /* body */
    GM_STACK_CHECK(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    void * t2 = F.l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_24nvriq, (void *)&F.h, cx});
    gm_b16_8 t4 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t4;
    void * t5 = F.l0;
    gm_b16_8 t6 = gm_l3;
    void * t7 = F.l1;
    gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 72));
    gm_t8658kcqovtpe t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)36ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t6);
    gm_b0_1 t10 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t9, t8);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_field_cdqu5on F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l1;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)19ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
            uint8_t t11 = gm_f_is_bits_pbijkuf(t8, GM_CONV(gm_csguankko4lmhd, t10));
            F.ret = t11;
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
inline __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_diverges_p3okmol F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    void * t5 = gm_l1;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
    gm_t53at80pfdus3 t7;
    gm_f_kind_jcnt4hm((void *)&t7, t4, GM_CONV(gm_csguankko4lmhd, t6));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
    uint8_t t9 = (uint8_t)((t8 == ((uint8_t)5ULL)));
    gm_t53at80pfdus3 t10;
    memset(&t10, 0, sizeof t10);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)5ULL));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_emit_q94msgt F;
    void * gm_l0; /* lw */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 485u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        gm_tl874t2atnlh3 t4 = gm_l2;
        gm_t8658kcqovtpe t5 = GM_LD(gm_t8658kcqovtpe, ((uint8_t *)&F.l1));
        gm_t8g064jsjmhum t6;
        GM_ST(uint32_t, (uint8_t *)&t6 + 220, 0);
        GM_ST(gm_t8658kcqovtpe, ((uint8_t *)&t6 + 0), t5);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 208), t4);
        gm_tftb1odc739t5 t7 = gm_f_push_ql89qc9(t3, &t6);
        gm_l3 = t7;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 68, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4g9am13(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4g9am13 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6h1imen F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d1l4euh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d1l4euh F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fooqdot(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fooqdot F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hh33a3d F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_i0ticdn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uvn29bg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uvn29bg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vbdqifj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vbdqifj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_5l0p1mi(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 80);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_47srmke F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_ajtfk2n *)((&F.h)->up))->l1));
    gm_f_visit_ajtfk2n(t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4oh4m1v(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_4oh4m1v F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_stmt_k90qtjr *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_stmt_k90qtjr *)((&F.h)->up))->l60));
    gm_t7b7esi3n9r90 t4;
    gm_f_expr_tp86bm9((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74evcu5(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_74evcu5 F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t3 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
    void * t4 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t5 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_7h6ducr F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t3 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l6));
    void * t4 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l2) + 32));
    gm_b0_1 t5 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a8eu0io(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_a8eu0io F;
    void * gm_l0; /* el */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            uint8_t t4 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24)) != 0);
            if (!t4) {
                goto gm_lb2;
            }
            void * t5 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24));
            gm_l0 = t5;
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
            gm_t9805qtonbsag t7 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
            void * t8 = gm_l0;
            gm_b0_1 t9 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_roenhir F;
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l0));
    void * t3 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l2) + 24));
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l0));
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t7b7esi3n9r90 t8;
    gm_f_block_iujbbjb((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t7);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_bits_6bio9a8 F;
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    uint64_t t3 = (uint64_t)t2;
    gm_tf5srh4mp4b3i t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t4), t3);
    ((uint8_t *)&t4)[8] = 1;
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_label_3373lil F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 136));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 77, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 136), t7);
    void * t8 = gm_l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 136));
    uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 78, 12);
    uint32_t t12 = r10;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi();
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
inline __attribute__((aligned(4))) void gm_f_list_0ngbo25(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0ngbo25 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uvn29bg();
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_a3ut5mo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a3ut5mo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fooqdot();
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
inline __attribute__((aligned(4))) void gm_f_list_gaq8h4q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gaq8h4q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d1l4euh();
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
inline __attribute__((aligned(4))) void gm_f_list_hft9rao(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hft9rao F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4g9am13();
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
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mgqfh8v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6h1imen();
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
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rb5mlsd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hh33a3d();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_urufktd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_i0ticdn();
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
inline __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_place_qnf3mb2 F;
    uint32_t gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
    gm_tu78tssgij60g t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t2);
    GM_ST(gm_tu78tssgij60g, ((uint8_t *)&(*(gm_tlg23kb3oj23s *)gm_out) + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tlg23kb3oj23s *)gm_out) + 40), t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_target_vvq8lnf F;
    void * gm_l0; /* lw */
    gm_te9hrk0u35rev gm_l1; /* t */
    uint32_t gm_l2; /* tl */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            uint8_t t7 = ((t6 == ((uint32_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 4));
            gm_l2 = t8;
            void * t9 = gm_l0;
            uint32_t t10 = gm_l2;
            gm_f_loop_labels_uvqimfl((void *)&(*(gm_t7isn7qnmqh8i *)gm_out), t9, t10);
            goto gm_fdone;
        }
gm_lb2:;
        memset(&(*(gm_t7isn7qnmqh8i *)gm_out), 0, sizeof (*(gm_t7isn7qnmqh8i *)gm_out));
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mark_root_b973vvi(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_root_b973vvi F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9805qtonbsag gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_t9805qtonbsag t5 = gm_f_root_local_40frg7f(t3, t4);
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
    if (t6) {
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l2 = t7;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint32_t t8 = gm_l2;
    uint64_t t9 = (uint64_t)t8;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 16) + 8));
    uint8_t t12 = (t9 < t11);
    if (t12) {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 16));
        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 16) + 8));
        uint32_t t16 = gm_l2;
        uint64_t t17 = (uint64_t)t16;
        if ((uintptr_t)t17 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t17, t15, gm_srccujoh2vgr9aq, 668, 41);
        GM_ST(uint8_t, ((((uint8_t *)t14) + (uintptr_t)t17 * 32) + 25), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_one_n667r26 F;
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l2;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_hft9rao((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t4 = gm_f_push_kki5o6e(t3, &(*(gm_tfqmtuprvunod *)((uint8_t *)&F.l0)));
        gm_l2 = t4;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srccujoh2vgr9aq, 650, 5);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_2qgfbfe(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_2qgfbfe F;
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
        memset(&(*(gm_tmpdn4irouh8b *)gm_out), 0, sizeof (*(gm_tmpdn4irouh8b *)gm_out));
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
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 224), 224);
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
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t8g064jsjmhum t25 = GM_LD(gm_t8g064jsjmhum, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out) + 225, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out) + 226, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out) + 228, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out) + 232, 0);
        GM_ST(gm_t8g064jsjmhum, ((uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out)), t25);
        ((uint8_t *)&(*(gm_tmpdn4irouh8b *)gm_out))[224] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ptr_size_lig81gu F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tbccvikg6b96e gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_layout_a5ovnrm((void *)&gm_l1, (void *)NULL, cx, NULL, t3, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)&F.l1), 48);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2v345ri F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6h1imen();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s8usk1j(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)&F.l1), 64);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_e2qt8as(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_e2qt8as F;
    void * gm_l0; /* l */
    gm_tp4rm55bg84ue gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d1l4euh();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_uue84k4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 20), ((uint8_t *)a1), 20);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kki5o6e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kki5o6e F;
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
        gm_b16_8 t9 = gm_f_empty_slice_4g9am13();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5l0p1mi(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 80), ((uint8_t *)a1), 80);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fv6i5s8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5719f1b(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ql89qc9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ql89qc9 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_uvn29bg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5hl8984(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 224), ((uint8_t *)a1), 224);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r4haklv F;
    void * gm_l0; /* l */
    gm_t9805qtonbsag gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_hh33a3d();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0s9hfc3(t8, ((uint64_t)1ULL), t10);
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
    gm_t9805qtonbsag t18 = gm_l1;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u46i39f F;
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
        gm_b16_8 t9 = gm_f_empty_slice_i0ticdn();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q7kpg21(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u8i76or(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u8i76or F;
    void * gm_l0; /* l */
    gm_t0o0svomecjg6 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fooqdot();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_tsagih4(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_read_result_8csahoh F;
    void * gm_l0; /* lw */
    gm_t9805qtonbsag gm_l1; /* res */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t2 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t2) {
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_l4 = t3;
    } else {
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t4));
        goto gm_fdone;
    }
    {
        void * t5 = gm_l0;
        uint32_t t6 = gm_l4;
        gm_tlg23kb3oj23s t7;
        gm_f_local_place_qnf3mb2((void *)&t7, t6);
        gm_t88rcucg1b7uk t8 = gm_l2;
        gm_tl874t2atnlh3 t9 = gm_l3;
        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t5, &t7, t8, t9);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1) {
    struct gm_fr_region_q7ivn7p F;
    void * gm_l0; /* lw */
    gm_closure gm_l1; /* body */
    gm_b16_8 gm_l3; /* out */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t2) + 96), 40);
    }
    {
        void * t3 = gm_l0;
        gm_trqfvmjtt4sij t4;
        gm_f_list_0ngbo25((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 96), t4);
    }
    gm_closure t5 = gm_l1;
    gm_closure t6 = t5;
    gm_node *ccx7 = t6.cx;
    void * t8 = t6.code;
    gm_b0_1 t9;
    if (GM_PTR_TAGGED(t8)) {
        ((void (*)(void))GM_PTR_UNTAG(t8))();
    } else {
        t9 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *))t8)(t6.env, ccx7, NULL);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 96));
    gm_l3 = t11;
    {
        void * t12 = gm_l0;
        memmove((((uint8_t *)t12) + 96), ((uint8_t *)&F.l2), 40);
    }
    gm_b16_8 t13 = gm_l3;
    F.ret = t13;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0s9hfc3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0s9hfc3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5hl8984(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5hl8984 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)224ULL), ((uint64_t)16ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5l0p1mi(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5l0p1mi F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)16ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q7kpg21 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s8usk1j F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tsagih4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tsagih4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_uue84k4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_uue84k4 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_result_of_9m0s9vd F;
    void * gm_l0; /* lw */
    uint32_t gm_l1; /* tl */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 192) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 192));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 192) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srccujoh2vgr9aq, 960, 12);
    gm_t9805qtonbsag t13 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t9) + (uintptr_t)t12 * 8));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_temp_lneuo24 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 56);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_k3l9nic(t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srccujoh2vgr9aq, 72, 5);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 56) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 73, 13);
    uint64_t t11 = r9;
    uint32_t t12 = (uint32_t)t11;
    F.ret = t12;
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
inline __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ggopu3h F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t5));
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
inline __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_ajtfk2n F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_closure t2 = F.l1;
    void * t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l0;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_47srmke, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t8, t9);
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
inline __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_zero_sln4lkh F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_t88rcucg1b7uk, a0);
    gm_t88rcucg1b7uk t2 = gm_l0;
    memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)3ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_block_iujbbjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_block_iujbbjb F;
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* has_defers */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* st */
    void * gm_l7; /* tail */
    uint8_t gm_l8; /* sized */
    uint32_t gm_l10; /* l */
    gm_b16_8 gm_l11; /* body */
    uint32_t gm_l12; /* r */
    GM_STACK_CHECK(&F, 917u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tbccvikg6b96e gm_l15;
    gm_l3 = ((uint8_t)0);
    void * t10 = F.l1;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    gm_l4 = t11;
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
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t16) + (uintptr_t)t18 * 32), 32);
                        {
                            {
                                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t20 = (uint8_t)(t19 == ((uint8_t)5ULL));
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                gm_l13 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l13 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 701, 12);
                        }
gm_lb6:;
                        uint8_t t21 = gm_l13;
                        if (t21) {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 700, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l5;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l5 = t25;
        }
    }
gm_lb1:;
    uint8_t t26 = gm_l3;
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = F.l0;
        void * t29 = F.l1;
        gm_b0_1 t30 = gm_f_stmts_ctl06qt((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t31 = F.l1;
        uint8_t t32 = (uint8_t)(GM_LD(void *, (((uint8_t *)t31) + 16)) != 0);
        if (t32) {
            void * t33 = GM_LD(void *, (((uint8_t *)t31) + 16));
            gm_l7 = t33;
        } else {
            void * t34 = F.l0;
            gm_f_unit_ggopu3h((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        void * t35 = F.l0;
        void * t36 = gm_l7;
        gm_f_expr_tp86bm9((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        void * t37 = F.l0;
        void * t38 = GM_LD(void *, ((uint8_t *)t37));
        gm_t88rcucg1b7uk t39 = gm_l2;
        gm_f_layout_a5ovnrm((void *)&gm_l15, (void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
    uint8_t t41 = (t40 > ((uint64_t)0ULL));
    if (t41) {
        void * t42 = F.l0;
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        void * t44 = (void *)(((uint8_t *)t43) + 8);
        gm_t88rcucg1b7uk t45 = gm_l2;
        gm_t53at80pfdus3 t46;
        gm_f_kind_jcnt4hm((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
        uint8_t t48 = (uint8_t)(!(t47 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t49;
        memset(&t49, 0, sizeof t49);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)5ULL));
        gm_l14 = t48;
    } else {
        gm_l14 = ((uint8_t)0);
    }
    uint8_t t50 = gm_l14;
    gm_l8 = t50;
    uint8_t t51 = gm_l8;
    if (t51) {
        void * t52 = F.l0;
        gm_t88rcucg1b7uk t53 = gm_l2;
        uint32_t t54 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t55), t54);
        ((uint8_t *)&t55)[4] = 1;
        F.l9 = t55;
    } else {
        gm_t9805qtonbsag t56;
        memset(&t56, 0, sizeof t56);
        F.l9 = t56;
    }
    void * t57 = F.l0;
    uint32_t t58 = gm_f_label_3373lil(t57);
    gm_l10 = t58;
    void * t59 = F.l0;
    gm_closure t60 = ((gm_closure){(void *)gm_f_fn_k3pfo3n, (void *)&F.h, cx});
    gm_b16_8 t61 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t59, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t61;
    {
        void * t62 = F.l0;
        uint32_t t63 = gm_l10;
        gm_b16_8 t64 = gm_l11;
        gm_tiilmfd16v3fc t65;
        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t63);
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t64);
        void * t66 = F.l1;
        gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 24));
        gm_t8658kcqovtpe t68;
        memset(&t68, 0, sizeof t68);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t68 + 16), t65);
        gm_b0_1 t69 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t62, &t68, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t70 = (uint8_t)((((uint8_t *)&F.l9))[4] != 0);
    if (t70) {
        uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)&F.l9));
        gm_l12 = t71;
    } else {
        gm_t88rcucg1b7uk t72 = gm_l2;
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t72));
        goto gm_fdone;
    }
    {
        void * t73 = F.l0;
        uint32_t t74 = gm_l12;
        gm_tlg23kb3oj23s t75;
        gm_f_local_place_qnf3mb2((void *)&t75, t74);
        gm_t88rcucg1b7uk t76 = gm_l2;
        void * t77 = F.l1;
        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 24));
        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t73, &t75, t76, t78);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_bounds_6f9s0q1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_bounds_6f9s0q1 F;
    void * gm_l0; /* lw */
    gm_tl874t2atnlh3 gm_l3; /* span */
    GM_STACK_CHECK(&F, 656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 644, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    {
        void * t8 = gm_l0;
        gm_t7b7esi3n9r90 t9 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a1));
        gm_t7b7esi3n9r90 t10 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
        gm_tthvfhsvm2g0s t11;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 0), t9);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 48), t10);
        gm_tkj0hg83g38nr t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tthvfhsvm2g0s, ((uint8_t *)&t12 + 16), t11);
        gm_tl874t2atnlh3 t13 = gm_l3;
        gm_t8658kcqovtpe t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)26ULL));
        GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t14 + 16), t12);
        gm_b0_1 t15 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t8, &t14, t13);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_conds_sqc7llk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_conds_sqc7llk F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* cond */
    uint32_t gm_l2; /* fail */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* b */
    void * gm_l8; /* pat */
    void * gm_l9; /* value */
    uint32_t gm_l11; /* l */
    GM_STACK_CHECK(&F, 1250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tfonjjajjj3jq gm_l13;
    gm_b16_8 t10 = gm_l1;
    gm_l3 = t10;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t16 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t14) + (uintptr_t)t16 * 24), 24);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                void * t19 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t19;
                                {
                                    void * t20 = gm_l0;
                                    void * t21 = gm_l6;
                                    gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t20, t21);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    void * t22 = gm_l0;
                                    uint32_t t23 = gm_l2;
                                    gm_t7b7esi3n9r90 t24 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                                    gm_tejc6vv5sqno9 t25;
                                    GM_ST(uint8_t, (uint8_t *)&t25 + 49, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t25 + 50, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t25 + 56, 0);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t25 + 0), t24);
                                    GM_ST(uint8_t, ((uint8_t *)&t25 + 48), ((uint8_t)1));
                                    GM_ST(uint32_t, ((uint8_t *)&t25 + 52), t23);
                                    void * t26 = gm_l6;
                                    gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
                                    gm_t8658kcqovtpe t28;
                                    memset(&t28, 0, sizeof t28);
                                    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)31ULL));
                                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t28 + 16), t25);
                                    gm_b0_1 t29 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t22, &t28, t27);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
                                if (!t31) {
                                    goto gm_lb8;
                                }
                                void * t32 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l8 = t32;
                                void * t33 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                gm_l9 = t33;
                                {
                                    void * t34 = gm_l0;
                                    void * t35 = gm_l9;
                                    gm_f_operand_place_cdle4ls((void *)&gm_l13, (void *)NULL, cx, NULL, t34, t35);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l13))[56] != 0);
                                if (t36) {
                                    memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l13), 56);
                                } else {
                                    {
                                        void * t37 = gm_l0;
                                        void * t38 = gm_l9;
                                        gm_f_escape_avsr9t6((void *)&F.l10, (void *)NULL, cx, NULL, t37, t38);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t39 = gm_l0;
                                    void * t40 = gm_l9;
                                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t40));
                                    uint32_t t42 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t39, t41);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l11 = t42;
                                    {
                                        void * t43 = gm_l0;
                                        uint32_t t44 = gm_l11;
                                        gm_tlg23kb3oj23s t45;
                                        gm_f_local_place_qnf3mb2((void *)&t45, t44);
                                        gm_t7b7esi3n9r90 t46 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                                        gm_t6fp3q71vjrmk t47;
                                        GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
                                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t47 + 0), t45);
                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t47 + 64), t46);
                                        void * t48 = gm_l9;
                                        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
                                        gm_t8658kcqovtpe t50;
                                        memset(&t50, 0, sizeof t50);
                                        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)25ULL));
                                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t50 + 16), t47);
                                        gm_b0_1 t51 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t43, &t50, t49);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        void * t52 = gm_l0;
                                        void * t53 = gm_l8;
                                        uint32_t t54 = gm_l11;
                                        gm_tlg23kb3oj23s t55;
                                        gm_f_local_place_qnf3mb2((void *)&t55, t54);
                                        uint32_t t56 = gm_l2;
                                        gm_b0_1 t57 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t52, t53, &t55, t56);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb3;
                                }
                                {
                                    memmove(((uint8_t *)&F.l12), ((uint8_t *)&F.l12), 56);
                                }
                                {
                                    void * t58 = gm_l0;
                                    void * t59 = gm_l8;
                                    uint32_t t60 = gm_l2;
                                    gm_b0_1 t61 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t58, t59, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l12)), t60);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 925, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 924, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t62 = gm_l4;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l4 = t65;
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
inline __attribute__((aligned(4))) void gm_f_context_l4fgtfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_context_l4fgtfp F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* g */
    uint32_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        void * t14 = gm_l0;
        gm_trqfvmjtt4sij t15;
        gm_f_list_urufktd((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tig4i22alq15r t16;
        GM_ST(void *, ((uint8_t *)&t16 + 0), t14);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 8), t15);
        F.l1 = t16;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t17) + 384) + 8));
    gm_l2 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = gm_l2;
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t22 = (void *)(((uint8_t *)&F.l1) + 8);
                        gm_trqfvmjtt4sij t23;
                        gm_f_list_go2qg0b((void *)&t23, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t24 = gm_f_push_u46i39f(t22, &t23);
                        gm_l8 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccujoh2vgr9aq, 15, 36);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 15, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l3;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l3 = t29;
        }
    }
gm_lb1:;
    gm_l5 = ((uint64_t)0ULL);
    void * t30 = gm_l0;
    uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t30) + 384) + 8));
    gm_l4 = t31;
    {
        for (;;) {
            uint64_t t32 = gm_l5;
            uint64_t t33 = gm_l4;
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t35 = gm_l5;
                        gm_l6 = t35;
                        {
                            {
                                void * t36 = gm_l0;
                                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 384));
                                uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 384) + 8));
                                uint64_t t39 = gm_l6;
                                if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srccujoh2vgr9aq, 17, 17);
                                uint8_t t40 = (uint8_t)((((((uint8_t *)t37) + (uintptr_t)t39 * 64) + 48))[4] != 0);
                                if (!t40) {
                                    goto gm_lb12;
                                }
                                uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t37) + (uintptr_t)t39 * 64) + 48));
                                gm_l7 = t41;
                                void * t42 = GM_LD(void *, (((uint8_t *)&F.l1) + 8));
                                uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 8) + 8));
                                uint32_t t44 = gm_l7;
                                uint64_t t45 = (uint64_t)t44;
                                if ((uintptr_t)t45 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t45, t43, gm_srccujoh2vgr9aq, 17, 42);
                                void * t46 = (void *)(((uint8_t *)t42) + (uintptr_t)t45 * 40);
                                uint64_t t47 = gm_l6;
                                uint32_t t48 = (uint32_t)t47;
                                gm_tftb1odc739t5 t49 = gm_f_push_im9d4ro(t46, t48);
                                gm_l9 = t49;
                                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccujoh2vgr9aq, 17, 42);
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 16, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t51 = gm_l5;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l5 = t54;
        }
    }
gm_lb6:;
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
extern __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_else_expr_lrum9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_else_expr_lrum9ir F;
    uint8_t gm_l4; /* is_result */
    uint32_t gm_l8; /* present */
    gm_b16_8 gm_l10; /* then */
    gm_b16_8 gm_l11; /* otherwise */
    GM_STACK_CHECK(&F, 1290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_t53at80pfdus3 gm_l12;
    gm_tkn42u7na3t81 gm_l13;
    gm_tfonjjajjj3jq gm_l14;
    void * t5 = F.l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    F.l3 = t6;
    {
        void * t7 = F.l3;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        void * t9 = GM_LD(void *, ((uint8_t *)&F.l2));
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
        gm_f_kind_jcnt4hm((void *)&gm_l12, t8, GM_CONV(gm_csguankko4lmhd, t10));
    }
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)14ULL));
            if (!t12) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1163, 18);
    }
gm_lb1:;
    void * t13 = F.l0;
    void * t14 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
    gm_tkn42u7na3t81 t16 = gm_f_payload_ty_gn5u4tt(t13, GM_CONV(gm_csguankko4lmhd, t15));
    gm_l13 = t16;
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
    if (t17) {
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
        F.l5 = t18;
    } else {
        void * t19 = F.l0;
        void * t20 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t21 = F.l0;
    void * t22 = F.l1;
    gm_t9805qtonbsag t23 = gm_f_result_for_6fafafc((void *)NULL, cx, NULL, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t23;
    {
        void * t24 = F.l0;
        void * t25 = GM_LD(void *, ((uint8_t *)&F.l2));
        gm_f_operand_place_cdle4ls((void *)&gm_l14, (void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l14))[56] != 0);
    if (t26) {
        memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l14), 56);
    } else {
        void * t27 = F.l0;
        void * t28 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t29 = F.l0;
    void * t30 = F.l3;
    void * t31 = (void *)(((uint8_t *)t30) + 8);
    gm_t88rcucg1b7uk t32 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t33 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t29, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t33;
    {
        void * t34 = F.l0;
        uint32_t t35 = gm_l8;
        uint8_t t36 = gm_l4;
        gm_tlg23kb3oj23s t37 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l7));
        gm_t3349g713hmcr t38;
        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t38 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 68, 0);
        GM_ST(uint32_t, ((uint8_t *)&t38 + 0), t35);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t38 + 8), t37);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 64), t36);
        void * t39 = F.l1;
        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
        gm_t8658kcqovtpe t41;
        memset(&t41, 0, sizeof t41);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)23ULL));
        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t41 + 16), t38);
        gm_b0_1 t42 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t34, &t41, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t43 = F.l0;
    void * t44 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t44));
    uint64_t t46 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t43, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t46;
    void * t47 = F.l0;
    gm_closure t48 = ((gm_closure){(void *)gm_f_fn_3sp5ule, (void *)&F.h, cx});
    gm_b16_8 t49 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t47, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t49;
    void * t50 = F.l0;
    gm_closure t51 = ((gm_closure){(void *)gm_f_fn_rihlhf9, (void *)&F.h, cx});
    gm_b16_8 t52 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t52;
    {
        void * t53 = F.l0;
        uint32_t t54 = gm_l8;
        gm_b16_8 t55 = gm_l10;
        gm_b16_8 t56 = gm_l11;
        gm_t7b7esi3n9r90 t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t57 + 16), t54);
        gm_t1s3s9ds8udn4 t58;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t58 + 0), t57);
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 48), t55);
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 64), t56);
        void * t59 = F.l1;
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
        gm_t8658kcqovtpe t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t61 + 16), t58);
        gm_b0_1 t62 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t53, &t61, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t63 = F.l0;
        gm_t9805qtonbsag t64 = F.l6;
        void * t65 = F.l1;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t65));
        void * t67 = F.l1;
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
        gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t63, t64, t66, t68);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_escape_avsr9t6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_escape_avsr9t6 F;
    void * gm_l1; /* e */
    uint32_t gm_l2; /* d */
    GM_STACK_CHECK(&F, 736u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_tbccvikg6b96e gm_l4;
    void * t2 = F.l0;
    void * t3 = F.l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 184));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 132, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 184), t7);
    void * t8 = gm_l1;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_vkfbupa, (void *)&F.h, cx});
    gm_f_visit_ajtfk2n(t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        void * t10 = F.l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = gm_l1;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
        gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
    if (t15) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t16 = F.l0;
        void * t17 = gm_l1;
        uint8_t t18 = gm_f_diverges_p3okmol(t16, t17);
        gm_l3 = t18;
    }
    uint8_t t19 = gm_l3;
    if (t19) {
        {
            void * t20 = F.l0;
            gm_t9805qtonbsag t21;
            memset(&t21, 0, sizeof t21);
            void * t22 = gm_l1;
            gm_t12ib7gg43lh0 t23;
            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t23 + 0), t21);
            GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
            void * t24 = gm_l1;
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 72));
            gm_t8658kcqovtpe t26;
            memset(&t26, 0, sizeof t26);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)44ULL));
            GM_ST(gm_t12ib7gg43lh0, ((uint8_t *)&t26 + 16), t23);
            gm_b0_1 t27 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t26, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t28 = gm_l1;
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t28));
            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t29));
            goto gm_fdone;
        }
    }
    void * t30 = F.l0;
    void * t31 = gm_l1;
    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t31));
    uint32_t t33 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t30, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t33;
    {
        void * t34 = F.l0;
        uint32_t t35 = gm_l2;
        gm_t9805qtonbsag t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t36), t35);
        ((uint8_t *)&t36)[4] = 1;
        void * t37 = gm_l1;
        gm_t12ib7gg43lh0 t38;
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t38 + 0), t36);
        GM_ST(void *, ((uint8_t *)&t38 + 8), t37);
        void * t39 = gm_l1;
        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
        gm_t8658kcqovtpe t41;
        memset(&t41, 0, sizeof t41);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)44ULL));
        GM_ST(gm_t12ib7gg43lh0, ((uint8_t *)&t41 + 16), t38);
        gm_b0_1 t42 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t34, &t41, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t43 = gm_l2;
        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t43);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_pattern_6lpj7tc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3) {
    struct gm_fr_escape_pattern_6lpj7tc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* pat */
    uint32_t gm_l3; /* fail */
    gm_t88rcucg1b7uk gm_l4; /* bool_ty */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* scrut */
    void * gm_l9; /* wild */
    void * gm_l10; /* m */
    GM_STACK_CHECK(&F, 1750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t7));
        void * t9 = gm_l1;
        gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 120));
        gm_tlg23kb3oj23s t11 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
        gm_f_load_go4fgkp((void *)&F.l5, (void *)NULL, cx, NULL, t6, &t11, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t12 = gm_l0;
    void * t13 = gm_l1;
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t13));
    uint32_t t15 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t12, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t15;
    {
        void * t16 = gm_l0;
        uint32_t t17 = gm_l6;
        gm_tlg23kb3oj23s t18;
        gm_f_local_place_qnf3mb2((void *)&t18, t17);
        gm_t7b7esi3n9r90 t19 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
        gm_t6fp3q71vjrmk t20;
        GM_ST(uint64_t, (uint8_t *)&t20 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t20 + 0), t18);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t20 + 64), t19);
        void * t21 = gm_l1;
        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 120));
        gm_t8658kcqovtpe t23;
        memset(&t23, 0, sizeof t23);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t23 + 16), t20);
        gm_b0_1 t24 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t16, &t23, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = gm_l1;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t25));
        uint32_t t27 = gm_l6;
        void * t28 = gm_l1;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 120));
        gm_to0f5qn75cgg8 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t30 + 8), t27);
        void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t26, &t30, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t31;
    }
    {
        gm_trqfvmjtt4sij t32;
        gm_f_list_58nr42v_2((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t32;
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l8);
        void * t34 = gm_l1;
        gm_t88rcucg1b7uk t35 = gm_l4;
        void * t36 = gm_l1;
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 120));
        gm_to0f5qn75cgg8 t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t38 + 8), ((uint8_t)1));
        void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t38, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbbqefuhv2r8h t40;
        GM_ST(void *, ((uint8_t *)&t40 + 0), t34);
        GM_ST(void *, ((uint8_t *)&t40 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t40 + 16), t39);
        gm_tftb1odc739t5 t41 = gm_f_push_bpovr2k_2(t33, &t40);
        gm_l12 = t41;
    }
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srccujoh2vgr9aq, 1140, 5);
    {
        void * t43 = gm_l1;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t43));
        void * t45 = gm_l1;
        gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 120));
        gm_tb38v5s9j1r32 t47;
        memset(&t47, 0, sizeof t47);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
        void * t48 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t44, &t47, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t48;
    }
    {
        void * t49 = (void *)((uint8_t *)&F.l8);
        void * t50 = gm_l9;
        gm_t88rcucg1b7uk t51 = gm_l4;
        void * t52 = gm_l1;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 120));
        gm_to0f5qn75cgg8 t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t54 + 8), ((uint8_t)0));
        void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t54, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbbqefuhv2r8h t56;
        GM_ST(void *, ((uint8_t *)&t56 + 0), t50);
        GM_ST(void *, ((uint8_t *)&t56 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t56 + 16), t55);
        gm_tftb1odc739t5 t57 = gm_f_push_bpovr2k_2(t49, &t56);
        gm_l13 = t57;
    }
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccujoh2vgr9aq, 1142, 5);
    {
        gm_t88rcucg1b7uk t59 = gm_l4;
        void * t60 = gm_l7;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_tkaapcii86udk t62;
        GM_ST(void *, ((uint8_t *)&t62 + 0), t60);
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
        void * t63 = gm_l1;
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 120));
        gm_to0f5qn75cgg8 t65;
        memset(&t65, 0, sizeof t65);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t65 + 8), t62);
        void * t66 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t65, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t66;
    }
    {
        void * t67 = gm_l0;
        void * t68 = gm_l10;
        gm_f_escape_avsr9t6((void *)&F.l11, (void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t69 = gm_l0;
        uint32_t t70 = gm_l3;
        gm_t7b7esi3n9r90 t71 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l11));
        gm_tejc6vv5sqno9 t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 50, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t72 + 0), t71);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 48), ((uint8_t)1));
        GM_ST(uint32_t, ((uint8_t *)&t72 + 52), t70);
        void * t73 = gm_l1;
        gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 120));
        gm_t8658kcqovtpe t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t75 + 16), t72);
        gm_b0_1 t76 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t69, &t75, t74);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_escape_stmt_g02tr5c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_escape_stmt_g02tr5c F;
    void * gm_l0; /* lw */
    void * gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l4; /* b */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_tq7s98ihonkju gm_l7;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_ir0h3tc_2((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l3);
        void * t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_d6c76ud_2(t3, &(*(gm_trcmmd12cefb1 *)((uint8_t *)t4)));
        gm_l6 = t5;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccujoh2vgr9aq, 852, 5);
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_tl874t2atnlh3 t8 = gm_l2;
        gm_tgabaikr08mjg t9;
        GM_ST(uint32_t, (uint8_t *)&t9 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
        GM_ST(void *, ((uint8_t *)&t9 + 16), ((void *)0));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t9 + 24), t8);
        gm_tq7s98ihonkju t10 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t9, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4iadrd58qb)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t10;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srccujoh2vgr9aq, 853, 10);
    void * t12 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
    gm_l4 = t12;
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, ((uint8_t *)t13));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t17 = gm_l4;
        gm_tl874t2atnlh3 t18 = gm_l2;
        gm_to0f5qn75cgg8 t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t19 + 8), t17);
        void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t19, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l5;
        gm_t7b7esi3n9r90 t23;
        gm_f_escape_avsr9t6((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
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
__attribute__((aligned(4))) void gm_f_expr_tp86bm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_tp86bm9 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* s */
    uint64_t gm_l4; /* v */
    gm_b16_8 gm_l5; /* bytes */
    gm_b16_8 gm_l6; /* bytes */
    uint32_t gm_l7; /* d */
    void * gm_l8; /* x */
    uint32_t gm_l10; /* d */
    gm_tao63pk3r18v5 gm_l11; /* sl */
    gm_t3dcoc7077t1q gm_l12; /* x */
    void * gm_l13; /* x */
    uint8_t gm_l14; /* is_result */
    uint32_t gm_l16; /* present */
    gm_t083kd7b1kkag gm_l17; /* x */
    void * gm_l18; /* t */
    uint32_t gm_l19; /* d */
    void * gm_l20; /* x */
    uint32_t gm_l22; /* l */
    uint32_t gm_l23; /* d */
    void * gm_l24; /* elem */
    void * gm_l25; /* container */
    uint8_t gm_l26; /* negated */
    uint32_t gm_l29; /* d */
    void * gm_l30; /* k */
    void * gm_l31; /* v */
    void * gm_l34; /* k */
    void * gm_l37; /* w */
    gm_tiilmfd16v3fc gm_l38; /* eff */
    uint32_t gm_l39; /* op */
    void * gm_l40; /* f */
    void * gm_l43; /* t */
    gm_b16_8 gm_l44; /* args */
    double gm_l45; /* x */
    uint8_t gm_l46; /* b */
    uint32_t gm_l48; /* d */
    void * gm_l49; /* base */
    uint32_t gm_l50; /* index */
    uint32_t gm_l52; /* d */
    void * gm_l54; /* x */
    void * gm_l56; /* x */
    void * gm_l58; /* x */
    uint32_t gm_l60; /* d */
    uint32_t gm_l61; /* f */
    uint32_t gm_l62; /* d */
    uint32_t gm_l63; /* f */
    uint32_t gm_l64; /* d */
    uint32_t gm_l65; /* h */
    uint32_t gm_l66; /* d */
    gm_t9qkh0rsooc8d gm_l67; /* op */
    void * gm_l68; /* x */
    uint32_t gm_l70; /* d */
    gm_ts6oqppjh0ba3 gm_l71; /* op */
    void * gm_l72; /* l */
    void * gm_l73; /* r */
    uint32_t gm_l76; /* d */
    void * gm_l77; /* l */
    void * gm_l78; /* r */
    uint8_t gm_l79; /* negated */
    uint32_t gm_l82; /* d */
    uint8_t gm_l83; /* is_and */
    void * gm_l84; /* l */
    void * gm_l85; /* r */
    gm_tmn7n0n9raou5 gm_l86; /* kind */
    void * gm_l87; /* x */
    uint32_t gm_l89; /* d */
    gm_t8588ma2cr9d7 gm_l90; /* op */
    gm_b16_8 gm_l91; /* args */
    gm_b16_8 gm_l92; /* vals */
    uint32_t gm_l93; /* d */
    gm_b16_8 gm_l95; /* args */
    uint32_t gm_l96; /* f */
    gm_tiilmfd16v3fc gm_l97; /* eff */
    uint32_t gm_l98; /* op */
    void * gm_l99; /* x */
    gm_b16_8 gm_l101; /* vals */
    uint32_t gm_l102; /* d */
    gm_b16_8 gm_l103; /* xs */
    gm_b16_8 gm_l104; /* xs */
    gm_b16_8 gm_l105; /* vals */
    uint32_t gm_l106; /* d */
    gm_ttggq7ng4kp3p gm_l107; /* v */
    void * gm_l109; /* p */
    uint32_t gm_l110; /* d */
    void * gm_l111; /* x */
    uint32_t gm_l113; /* d */
    void * gm_l114; /* b */
    gm_trl2khmuphncd gm_l115; /* x */
    gm_teo9d4mncjfdu gm_l117; /* x */
    void * gm_l119; /* y */
    gm_tp4rm55bg84ue gm_l121; /* labels */
    uint32_t gm_l122; /* tl */
    void * gm_l123; /* v */
    uint32_t gm_l125; /* r */
    gm_te9hrk0u35rev gm_l126; /* t */
    gm_tp4rm55bg84ue gm_l127; /* labels */
    uint32_t gm_l128; /* levels */
    void * gm_l129; /* x */
    void * gm_l132; /* x */
    void * gm_l134; /* scrutinee */
    gm_b16_8 gm_l135; /* arms */
    GM_STACK_CHECK(&F, 20268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l136;
    gm_tbccvikg6b96e gm_l137;
    gm_tf5srh4mp4b3i gm_l138;
    gm_t7b7esi3n9r90 gm_l139;
    gm_tf8kfhcnval78 gm_l140;
    uint8_t gm_l141;
    gm_tfonjjajjj3jq gm_l142;
    gm_tbccvikg6b96e gm_l143;
    uint64_t gm_l144;
    gm_tf8kfhcnval78 gm_l145;
    gm_tfonjjajjj3jq gm_l146;
    gm_tbccvikg6b96e gm_l147;
    gm_tfonjjajjj3jq gm_l148;
    gm_tfonjjajjj3jq gm_l149;
    gm_tfonjjajjj3jq gm_l150;
    uint8_t gm_l151;
    gm_t53at80pfdus3 gm_l152;
    uint8_t gm_l153;
    gm_tbccvikg6b96e gm_l154;
    uint8_t gm_l155;
    gm_tbccvikg6b96e gm_l156;
    gm_t7isn7qnmqh8i gm_l157;
    gm_tbccvikg6b96e gm_l158;
    gm_t9805qtonbsag gm_l159;
    gm_t7isn7qnmqh8i gm_l160;
    gm_tbccvikg6b96e gm_l161;
    {
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, ((uint8_t *)t105));
        gm_l2 = t106;
        void * t107 = gm_l1;
        {
            {
                {
                    uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t109 = (uint8_t)(t108 == ((uint8_t)0ULL));
                    if (!t109) {
                        goto gm_lb2;
                    }
                    {
                        memmove(((uint8_t *)&F.l3), ((((uint8_t *)t107) + 8) + 8), 48);
                    }
                    {
                        void * t110 = gm_l2;
                        void * t111 = (void *)(((uint8_t *)t110) + 8);
                        void * t112 = gm_l1;
                        gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t112));
                        gm_f_kind_jcnt4hm((void *)&gm_l136, t111, GM_CONV(gm_csguankko4lmhd, t113));
                    }
                    {
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l136) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)3ULL));
                            if (!t115) {
                                goto gm_lb4;
                            }
                            {
                                void * t116 = gm_l0;
                                void * t117 = gm_l1;
                                gm_tngtuiuskn938 t118;
                                gm_f_from_int_ofjuoqh((void *)&t118, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                double t119 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t118);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_f_float_value_o4b87lc((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t116, t117, t119);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                        }
gm_lb4:;
                        {
                            goto gm_lb3;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 178, 13);
                    }
gm_lb3:;
                    void * t120 = gm_l2;
                    void * t121 = (void *)(((uint8_t *)t120) + 8);
                    void * t122 = gm_l1;
                    gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t122));
                    uint8_t t124 = gm_f_is_integer_8d4f9ek(t121, GM_CONV(gm_csguankko4lmhd, t123));
                    uint8_t t125 = (uint8_t)!t124;
                    if (t125) {
                        void * t126 = gm_l0;
                        void * t127 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t126, t127);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t128 = gm_l2;
                        void * t129 = gm_l1;
                        gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t129));
                        gm_f_layout_a5ovnrm((void *)&gm_l137, (void *)NULL, cx, NULL, t128, t130);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&gm_l137));
                    uint8_t t132 = (t131 > ((uint64_t)8ULL));
                    if (t132) {
                        void * t133 = gm_l1;
                        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t133));
                        unsigned __int128 t135 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                        gm_tl5jbgvjjt15g t136;
                        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t136 + 8, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136 + 0), t134);
                        GM_ST(unsigned __int128, ((uint8_t *)&t136 + 16), t135);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)2ULL));
                        GM_ST(gm_tl5jbgvjjt15g, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t136);
                        goto gm_fdone;
                    }
                    {
                        gm_tf5srh4mp4b3i t137 = gm_f_int_bits_6bio9a8(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                        gm_l138 = t137;
                    }
                    uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l138))[8] != 0);
                    if (t138) {
                        uint64_t t139 = GM_LD(uint64_t, ((uint8_t *)&gm_l138));
                        gm_l4 = t139;
                    } else {
                        void * t140 = gm_l0;
                        void * t141 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t140, t141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t142 = gm_l0;
                        void * t143 = gm_l1;
                        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t143));
                        uint64_t t145 = gm_l4;
                        gm_f_word_1sfojus((void *)&gm_l140, (void *)NULL, cx, NULL, t142, t144, t145);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t146 = (uint8_t)((((uint8_t *)&gm_l140))[48] != 0);
                    if (t146) {
                        memmove(((uint8_t *)&gm_l139), ((uint8_t *)&gm_l140), 48);
                    } else {
                        void * t147 = gm_l0;
                        void * t148 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&gm_l139, (void *)NULL, cx, NULL, t147, t148);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        memmove(gm_out, ((uint8_t *)&gm_l139), 48);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t150 = (uint8_t)(t149 == ((uint8_t)5ULL));
                            if (!t150) {
                                goto gm_lb8;
                            }
                            gm_b16_8 t151 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l5 = t151;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t153 = (uint8_t)(t152 == ((uint8_t)6ULL));
                            if (!t153) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t154 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l5 = t154;
                            goto gm_lb7;
                        }
gm_lb9:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    void * t155 = gm_l0;
                    void * t156 = gm_l1;
                    gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t156));
                    uint32_t t158 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t155, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t158;
                    {
                        void * t159 = gm_l0;
                        uint32_t t160 = gm_l7;
                        gm_b16_8 t161 = gm_l5;
                        void * t162 = gm_l1;
                        {
                            {
                                uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t162) + 8) + 0));
                                uint8_t t164 = (uint8_t)(t163 == ((uint8_t)6ULL));
                                if (!t164) {
                                    goto gm_lb11;
                                }
                                gm_l141 = ((uint8_t)1);
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                gm_l141 = ((uint8_t)0);
                                goto gm_lb10;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 189, 36);
                        }
gm_lb10:;
                        uint8_t t165 = gm_l141;
                        gm_trt0mvq732stt t166;
                        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t166 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t166 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t166 + 28, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t166 + 0), t160);
                        GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t161);
                        GM_ST(uint8_t, ((uint8_t *)&t166 + 24), t165);
                        void * t167 = gm_l1;
                        gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t167) + 72));
                        gm_t8658kcqovtpe t169;
                        memset(&t169, 0, sizeof t169);
                        GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)10ULL));
                        GM_ST(gm_trt0mvq732stt, ((uint8_t *)&t169 + 16), t166);
                        gm_b0_1 t170 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t159, &t169, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t171 = gm_l7;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t171);
                        goto gm_fdone;
                    }
                }
gm_lb6:;
                {
                    uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t173 = (uint8_t)(t172 == ((uint8_t)8ULL));
                    if (!t173) {
                        goto gm_lb13;
                    }
                    void * t174 = gm_l1;
                    gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t174));
                    memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)4ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t175);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t177 = (uint8_t)(t176 == ((uint8_t)37ULL));
                    if (!t177) {
                        goto gm_lb14;
                    }
                    void * t178 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l8 = t178;
                    {
                        void * t179 = gm_l0;
                        void * t180 = gm_l8;
                        gm_f_expr_tp86bm9((void *)&F.l9, (void *)NULL, cx, NULL, t179, t180);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t181 = gm_l0;
                    void * t182 = gm_l1;
                    gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t182));
                    uint32_t t184 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t181, t183);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t184;
                    {
                        void * t185 = gm_l0;
                        uint32_t t186 = gm_l10;
                        gm_t7b7esi3n9r90 t187 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l9));
                        gm_tf4lk4crue8vt t188;
                        GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t188 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t188 + 0), t186);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t188 + 16), t187);
                        void * t189 = gm_l1;
                        gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t189) + 72));
                        gm_t8658kcqovtpe t191;
                        memset(&t191, 0, sizeof t191);
                        GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)11ULL));
                        GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t191 + 16), t188);
                        gm_b0_1 t192 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t185, &t191, t190);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t193 = gm_l10;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t193);
                        goto gm_fdone;
                    }
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)21ULL));
                    if (!t195) {
                        goto gm_lb15;
                    }
                    {
                        memmove(((uint8_t *)&gm_l11), ((((uint8_t *)t107) + 8) + 8), 32);
                    }
                    {
                        void * t196 = gm_l0;
                        void * t197 = gm_l1;
                        gm_tao63pk3r18v5 t198 = GM_LD(gm_tao63pk3r18v5, ((uint8_t *)&gm_l11));
                        gm_f_slice_dup5j25((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t196, t197, &t198);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb15:;
                {
                    uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)41ULL));
                    if (!t200) {
                        goto gm_lb16;
                    }
                    {
                        memmove(((uint8_t *)&gm_l12), ((((uint8_t *)t107) + 8) + 8), 24);
                    }
                    {
                        void * t201 = gm_l0;
                        void * t202 = gm_l1;
                        gm_t3dcoc7077t1q t203 = GM_LD(gm_t3dcoc7077t1q, ((uint8_t *)&gm_l12));
                        gm_f_else_expr_lrum9ir((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t201, t202, &t203);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb16:;
                {
                    uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t205 = (uint8_t)(t204 == ((uint8_t)40ULL));
                    if (!t205) {
                        goto gm_lb17;
                    }
                    void * t206 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l13 = t206;
                    uint8_t t207 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l14 = t207;
                    {
                        void * t208 = gm_l0;
                        void * t209 = gm_l13;
                        gm_f_operand_place_cdle4ls((void *)&gm_l142, (void *)NULL, cx, NULL, t208, t209);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t210 = (uint8_t)((((uint8_t *)&gm_l142))[56] != 0);
                    if (t210) {
                        memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l142), 56);
                    } else {
                        void * t211 = gm_l0;
                        void * t212 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t211, t212);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t213 = gm_l0;
                    void * t214 = gm_l2;
                    void * t215 = (void *)(((uint8_t *)t214) + 8);
                    gm_t88rcucg1b7uk t216 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t217 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t213, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t217;
                    {
                        void * t218 = gm_l0;
                        uint32_t t219 = gm_l16;
                        uint8_t t220 = gm_l14;
                        gm_tlg23kb3oj23s t221 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                        gm_t3349g713hmcr t222;
                        GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t222 + 65, 0);
                        GM_ST(uint16_t, (uint8_t *)&t222 + 66, 0);
                        GM_ST(uint32_t, (uint8_t *)&t222 + 68, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t222 + 0), t219);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t222 + 8), t221);
                        GM_ST(uint8_t, ((uint8_t *)&t222 + 64), t220);
                        void * t223 = gm_l1;
                        gm_tl874t2atnlh3 t224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t223) + 72));
                        gm_t8658kcqovtpe t225;
                        memset(&t225, 0, sizeof t225);
                        GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)23ULL));
                        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t225 + 16), t222);
                        gm_b0_1 t226 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t218, &t225, t224);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t227 = gm_l0;
                        uint32_t t228 = gm_l16;
                        uint8_t t229 = gm_l14;
                        gm_t7b7esi3n9r90 t230;
                        memset(&t230, 0, sizeof t230);
                        GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t230 + 16), t228);
                        gm_tlg23kb3oj23s t231 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                        gm_t6j9ipvj0d6k5 t232;
                        GM_ST(uint8_t, (uint8_t *)&t232 + 105, 0);
                        GM_ST(uint16_t, (uint8_t *)&t232 + 106, 0);
                        GM_ST(uint32_t, (uint8_t *)&t232 + 108, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t232 + 0), t230);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t232 + 48), t231);
                        GM_ST(uint8_t, ((uint8_t *)&t232 + 104), t229);
                        gm_tkj0hg83g38nr t233;
                        memset(&t233, 0, sizeof t233);
                        GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t6j9ipvj0d6k5, ((uint8_t *)&t233 + 16), t232);
                        void * t234 = gm_l1;
                        gm_tl874t2atnlh3 t235 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t234) + 72));
                        gm_t8658kcqovtpe t236;
                        GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t236 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)26ULL));
                        GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t236 + 16), t233);
                        gm_b0_1 t237 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t227, &t236, t235);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t238 = gm_l2;
                        void * t239 = gm_l1;
                        gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t239));
                        gm_f_layout_a5ovnrm((void *)&gm_l143, (void *)NULL, cx, NULL, t238, t240);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t241 = GM_LD(uint64_t, ((uint8_t *)&gm_l143));
                    uint8_t t242 = ((t241 == ((uint64_t)0ULL)));
                    if (t242) {
                        void * t243 = gm_l1;
                        gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t243));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t244));
                        goto gm_fdone;
                    }
                    {
                        void * t245 = gm_l0;
                        void * t246 = gm_l0;
                        void * t247 = gm_l13;
                        gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t247));
                        gm_tlg23kb3oj23s t249 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                        uint64_t t250 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t246, t248);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t251 = gm_l1;
                        gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t251));
                        gm_tlcm4lvu3g1a8 t253;
                        GM_ST(uint32_t, (uint8_t *)&t253 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t253 + 0), t250);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t253 + 8), t252);
                        gm_tfqmtuprvunod t254;
                        memset(&t254, 0, sizeof t254);
                        GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t254 + 16), t253);
                        gm_tlg23kb3oj23s t255;
                        gm_f_project_6hpd48e((void *)&t255, (void *)NULL, cx, NULL, &t249, &t254);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t256 = gm_l1;
                        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t256));
                        void * t258 = gm_l1;
                        gm_tl874t2atnlh3 t259 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t258) + 72));
                        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t245, &t255, t257, t259);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t261 = (uint8_t)(t260 == ((uint8_t)42ULL));
                    if (!t261) {
                        goto gm_lb18;
                    }
                    {
                        memmove(((uint8_t *)&gm_l17), ((((uint8_t *)t107) + 8) + 8), 24);
                    }
                    {
                        void * t262 = gm_l0;
                        void * t263 = gm_l1;
                        gm_t083kd7b1kkag t264 = GM_LD(gm_t083kd7b1kkag, ((uint8_t *)&gm_l17));
                        gm_f_try_expr_2gjs5f7((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t262, t263, &t264);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb18:;
                {
                    uint8_t t265 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t266 = (uint8_t)(t265 == ((uint8_t)57ULL));
                    if (!t266) {
                        goto gm_lb19;
                    }
                    void * t267 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l18 = t267;
                    void * t268 = gm_l0;
                    void * t269 = gm_l1;
                    gm_t88rcucg1b7uk t270 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t269));
                    uint32_t t271 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t268, t270);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t271;
                    {
                        void * t272 = gm_l0;
                        uint32_t t273 = gm_l19;
                        void * t274 = gm_l18;
                        gm_ttggq7ng4kp3p t275;
                        GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t275 + 0), t273);
                        GM_ST(void *, ((uint8_t *)&t275 + 8), t274);
                        void * t276 = gm_l1;
                        gm_tl874t2atnlh3 t277 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t276) + 72));
                        gm_t8658kcqovtpe t278;
                        memset(&t278, 0, sizeof t278);
                        GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t278 + 16), t275);
                        gm_b0_1 t279 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t272, &t278, t277);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t280 = gm_l19;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t280);
                        goto gm_fdone;
                    }
                }
gm_lb19:;
                {
                    uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t282 = (uint8_t)(t281 == ((uint8_t)25ULL));
                    if (!t282) {
                        goto gm_lb20;
                    }
                    void * t283 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l20 = t283;
                    {
                        void * t284 = gm_l0;
                        void * t285 = gm_l20;
                        gm_f_expr_tp86bm9((void *)&F.l21, (void *)NULL, cx, NULL, t284, t285);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t286 = gm_l0;
                    void * t287 = gm_l20;
                    gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t287));
                    uint32_t t289 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t286, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t289;
                    void * t290 = gm_l0;
                    void * t291 = GM_LD(void *, (((uint8_t *)t290) + 16));
                    uint64_t t292 = GM_LD(uint64_t, ((((uint8_t *)t290) + 16) + 8));
                    uint32_t t293 = gm_l22;
                    uint64_t t294 = (uint64_t)t293;
                    if ((uintptr_t)t294 >= (uintptr_t)(t292)) gm_trap_index((uintptr_t)t294, t292, gm_srccujoh2vgr9aq, 218, 13);
                    GM_ST(uint8_t, ((((uint8_t *)t291) + (uintptr_t)t294 * 32) + 25), ((uint8_t)1));
                    {
                        void * t295 = gm_l0;
                        uint32_t t296 = gm_l22;
                        gm_tlg23kb3oj23s t297;
                        gm_f_local_place_qnf3mb2((void *)&t297, t296);
                        gm_t7b7esi3n9r90 t298 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l21));
                        gm_t6fp3q71vjrmk t299;
                        GM_ST(uint64_t, (uint8_t *)&t299 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t299 + 0), t297);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t299 + 64), t298);
                        void * t300 = gm_l20;
                        gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t300) + 72));
                        gm_t8658kcqovtpe t302;
                        memset(&t302, 0, sizeof t302);
                        GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t302 + 16), t299);
                        gm_b0_1 t303 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t295, &t302, t301);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t304 = gm_l0;
                    void * t305 = gm_l1;
                    gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t305));
                    uint32_t t307 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t304, t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t307;
                    {
                        void * t308 = gm_l0;
                        uint32_t t309 = gm_l23;
                        uint32_t t310 = gm_l22;
                        gm_tlg23kb3oj23s t311;
                        gm_f_local_place_qnf3mb2((void *)&t311, t310);
                        gm_tlpcrb3vu6o2d t312;
                        GM_ST(uint32_t, (uint8_t *)&t312 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t312 + 0), t309);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t312 + 8), t311);
                        void * t313 = gm_l1;
                        gm_tl874t2atnlh3 t314 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t313) + 72));
                        gm_t8658kcqovtpe t315;
                        memset(&t315, 0, sizeof t315);
                        GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t315 + 16), t312);
                        gm_b0_1 t316 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t308, &t315, t314);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t317 = gm_l23;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t317);
                        goto gm_fdone;
                    }
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t318 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t319 = (uint8_t)(t318 == ((uint8_t)45ULL));
                    if (!t319) {
                        goto gm_lb21;
                    }
                    void * t320 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l24 = t320;
                    void * t321 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l25 = t321;
                    uint8_t t322 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l26 = t322;
                    {
                        void * t323 = gm_l0;
                        void * t324 = gm_l24;
                        gm_f_expr_tp86bm9((void *)&F.l27, (void *)NULL, cx, NULL, t323, t324);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t325 = gm_l0;
                        void * t326 = gm_l25;
                        gm_f_expr_tp86bm9((void *)&F.l28, (void *)NULL, cx, NULL, t325, t326);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t327 = gm_l0;
                    void * t328 = gm_l1;
                    gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t328));
                    uint32_t t330 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t327, t329);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l29 = t330;
                    {
                        void * t331 = gm_l0;
                        uint32_t t332 = gm_l29;
                        uint8_t t333 = gm_l26;
                        gm_t7b7esi3n9r90 t334 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l27));
                        gm_t7b7esi3n9r90 t335 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l28));
                        gm_tr6ll1dg9imrl t336;
                        GM_ST(uint8_t, (uint8_t *)&t336 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t336 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t336 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t336 + 0), t332);
                        GM_ST(uint8_t, ((uint8_t *)&t336 + 4), t333);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t336 + 16), t334);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t336 + 64), t335);
                        void * t337 = gm_l1;
                        gm_tl874t2atnlh3 t338 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t337) + 72));
                        gm_t8658kcqovtpe t339;
                        memset(&t339, 0, sizeof t339);
                        GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t339 + 16), t336);
                        gm_b0_1 t340 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t331, &t339, t338);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t341 = gm_l29;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t341);
                        goto gm_fdone;
                    }
                }
gm_lb21:;
                {
                    uint8_t t342 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t343 = (uint8_t)(t342 == ((uint8_t)53ULL));
                    if (!t343) {
                        goto gm_lb22;
                    }
                    void * t344 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l30 = t344;
                    void * t345 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l31 = t345;
                    {
                        void * t346 = gm_l0;
                        void * t347 = gm_l30;
                        gm_f_expr_tp86bm9((void *)&F.l32, (void *)NULL, cx, NULL, t346, t347);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t348 = gm_l0;
                        void * t349 = gm_l31;
                        gm_f_expr_tp86bm9((void *)&F.l33, (void *)NULL, cx, NULL, t348, t349);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t350 = gm_l0;
                        gm_t7b7esi3n9r90 t351 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l32));
                        gm_t7b7esi3n9r90 t352 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l33));
                        gm_tthvfhsvm2g0s t353;
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t353 + 0), t351);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t353 + 48), t352);
                        void * t354 = gm_l1;
                        gm_tl874t2atnlh3 t355 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t354) + 72));
                        gm_t8658kcqovtpe t356;
                        memset(&t356, 0, sizeof t356);
                        GM_ST(uint8_t, ((uint8_t *)&t356 + 0), ((uint8_t)40ULL));
                        GM_ST(gm_tthvfhsvm2g0s, ((uint8_t *)&t356 + 16), t353);
                        gm_b0_1 t357 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t350, &t356, t355);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t358 = gm_l1;
                        gm_t88rcucg1b7uk t359 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t358));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t359));
                        goto gm_fdone;
                    }
                }
gm_lb22:;
                {
                    uint8_t t360 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t361 = (uint8_t)(t360 == ((uint8_t)54ULL));
                    if (!t361) {
                        goto gm_lb23;
                    }
                    void * t362 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l34 = t362;
                    {
                        void * t363 = gm_l0;
                        void * t364 = gm_l34;
                        gm_f_expr_tp86bm9((void *)&F.l35, (void *)NULL, cx, NULL, t363, t364);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t365 = gm_l0;
                        gm_t7b7esi3n9r90 t366 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l35));
                        void * t367 = gm_l1;
                        gm_tl874t2atnlh3 t368 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t367) + 72));
                        gm_t8658kcqovtpe t369;
                        memset(&t369, 0, sizeof t369);
                        GM_ST(uint8_t, ((uint8_t *)&t369 + 0), ((uint8_t)41ULL));
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t369 + 16), t366);
                        gm_b0_1 t370 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t365, &t369, t368);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t371 = gm_l1;
                        gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t371));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t372));
                        goto gm_fdone;
                    }
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t373 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t374 = (uint8_t)(t373 == ((uint8_t)59ULL));
                    if (!t374) {
                        goto gm_lb24;
                    }
                    {
                        memmove(((uint8_t *)&F.l36), ((((uint8_t *)t107) + 8) + 8), 40);
                    }
                    {
                        void * t375 = gm_l0;
                        void * t376 = gm_l1;
                        gm_t710lsrq1m8gk t377 = GM_LD(gm_t710lsrq1m8gk, ((uint8_t *)&F.l36));
                        gm_f_with_expr_g16bcnu((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t375, t376, &t377);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb24:;
                {
                    uint8_t t378 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t379 = (uint8_t)(t378 == ((uint8_t)55ULL));
                    if (!t379) {
                        goto gm_lb25;
                    }
                    void * t380 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l37 = t380;
                    void * t381 = gm_l37;
                    {
                        {
                            uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)t381) + 0));
                            uint8_t t383 = (uint8_t)(t382 == ((uint8_t)0ULL));
                            if (!t383) {
                                goto gm_lb27;
                            }
                            {
                                memmove(((uint8_t *)&gm_l38), (((uint8_t *)t381) + 8), 24);
                            }
                            uint32_t t384 = GM_LD(uint32_t, ((((uint8_t *)t381) + 8) + 24));
                            gm_l39 = t384;
                            {
                                uint32_t t385 = gm_l39;
                                gm_tiilmfd16v3fc t386 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l38));
                                gm_t419mo45g7j0a t387;
                                GM_ST(uint32_t, (uint8_t *)&t387 + 28, 0);
                                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t387 + 0), t386);
                                GM_ST(uint32_t, ((uint8_t *)&t387 + 24), t385);
                                GM_ST(uint8_t, (uint8_t *)&F.l41 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l41 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l41 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l41 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l41 + 48, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l41 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&F.l41 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&F.l41 + 16), t387);
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)t381) + 0));
                            uint8_t t389 = (uint8_t)(t388 == ((uint8_t)1ULL));
                            if (!t389) {
                                goto gm_lb28;
                            }
                            void * t390 = GM_LD(void *, (((uint8_t *)t381) + 8));
                            gm_l40 = t390;
                            {
                                void * t391 = gm_l0;
                                void * t392 = gm_l40;
                                gm_t7b7esi3n9r90 t393;
                                gm_f_expr_tp86bm9((void *)&t393, (void *)NULL, cx, NULL, t391, t392);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.l41 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l41 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l41 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l41 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&F.l41 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l41 + 16), t393);
                            }
                            goto gm_lb26;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 244, 26);
                    }
gm_lb26:;
                    {
                        memset(&F.l42, 0, sizeof F.l42);
                    }
                    {
                        {
                            void * t394 = gm_l37;
                            uint8_t t395 = (uint8_t)(GM_LD(void *, (((uint8_t *)t394) + 88)) != 0);
                            if (!t395) {
                                goto gm_lb30;
                            }
                            void * t396 = GM_LD(void *, (((uint8_t *)t394) + 88));
                            gm_l43 = t396;
                            {
                                void * t397 = gm_l0;
                                void * t398 = gm_l43;
                                gm_t7b7esi3n9r90 t399;
                                gm_f_expr_tp86bm9((void *)&t399, (void *)NULL, cx, NULL, t397, t398);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.l42 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l42 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l42 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l42 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l42), t399);
                                ((uint8_t *)&F.l42)[48] = 1;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                    }
gm_lb29:;
                    void * t400 = gm_l0;
                    void * t401 = gm_l37;
                    gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)t401) + 72));
                    gm_b16_8 t403 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t400, t402);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t403;
                    {
                        void * t404 = gm_l0;
                        void * t405 = gm_l37;
                        gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)t405) + 40));
                        void * t407 = gm_l37;
                        gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)t407) + 56));
                        gm_b16_8 t409 = gm_l44;
                        gm_t9i04n6j2d1gi t410 = GM_LD(gm_t9i04n6j2d1gi, ((uint8_t *)&F.l41));
                        gm_tf8kfhcnval78 t411 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l42));
                        gm_tihh2qes0hpd4 t412;
                        GM_ST(gm_t9i04n6j2d1gi, ((uint8_t *)&t412 + 0), t410);
                        GM_ST(gm_b16_8, ((uint8_t *)&t412 + 64), t406);
                        GM_ST(gm_b16_8, ((uint8_t *)&t412 + 80), t408);
                        GM_ST(gm_b16_8, ((uint8_t *)&t412 + 96), t409);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t412 + 112), t411);
                        void * t413 = gm_l1;
                        gm_tl874t2atnlh3 t414 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t413) + 72));
                        gm_t8658kcqovtpe t415;
                        GM_ST(uint8_t, (uint8_t *)&t415 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t415 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t415 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t415 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t415 + 192, 0);
                        GM_ST(uint64_t, (uint8_t *)&t415 + 200, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t415 + 0), ((uint8_t)13ULL));
                        GM_ST(gm_tihh2qes0hpd4, ((uint8_t *)&t415 + 16), t412);
                        gm_b0_1 t416 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t404, &t415, t414);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t417 = gm_l1;
                        gm_t88rcucg1b7uk t418 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t417));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t418));
                        goto gm_fdone;
                    }
                }
gm_lb25:;
            }
            {
                {
                    uint8_t t419 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t420 = (uint8_t)(t419 == ((uint8_t)1ULL));
                    if (!t420) {
                        goto gm_lb31;
                    }
                    double t421 = GM_LD(double, ((((uint8_t *)t107) + 8) + 8));
                    gm_l45 = t421;
                    void * t422 = gm_l0;
                    void * t423 = gm_l1;
                    double t424 = gm_l45;
                    gm_f_float_value_o4b87lc((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t422, t423, t424);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb31:;
                {
                    uint8_t t425 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)4ULL));
                    if (!t426) {
                        goto gm_lb32;
                    }
                    uint8_t t427 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l46 = t427;
                    void * t428 = gm_l1;
                    gm_t88rcucg1b7uk t429 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t428));
                    uint8_t t430 = gm_l46;
                    if (t430) {
                        gm_l144 = ((uint64_t)1ULL);
                    } else {
                        gm_l144 = ((uint64_t)0ULL);
                    }
                    uint64_t t431 = gm_l144;
                    gm_tfbnciphf3261 t432;
                    GM_ST(uint32_t, (uint8_t *)&t432 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t432 + 0), t429);
                    GM_ST(uint64_t, ((uint8_t *)&t432 + 8), t431);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tfbnciphf3261, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t432);
                    goto gm_fdone;
                }
gm_lb32:;
                {
                    uint8_t t433 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t434 = (uint8_t)(t433 == ((uint8_t)7ULL));
                    if (!t434) {
                        goto gm_lb33;
                    }
                    {
                        {
                            {
                                void * t435 = gm_l0;
                                void * t436 = gm_l1;
                                gm_t88rcucg1b7uk t437 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t436));
                                gm_f_word_1sfojus((void *)&gm_l145, (void *)NULL, cx, NULL, t435, t437, ((uint64_t)0ULL));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t438 = (uint8_t)((((uint8_t *)&gm_l145))[48] != 0);
                            if (!t438) {
                                goto gm_lb35;
                            }
                            {
                                memmove(((uint8_t *)&F.l47), ((uint8_t *)&gm_l145), 48);
                            }
                            {
                                memmove(gm_out, ((uint8_t *)&F.l47), 48);
                                goto gm_fdone;
                            }
                        }
gm_lb35:;
                    }
gm_lb34:;
                    void * t439 = gm_l0;
                    void * t440 = gm_l1;
                    gm_t88rcucg1b7uk t441 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t440));
                    uint32_t t442 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t439, t441);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l48 = t442;
                    {
                        void * t443 = gm_l0;
                        uint32_t t444 = gm_l48;
                        gm_tf8kfhcnval78 t445;
                        memset(&t445, 0, sizeof t445);
                        gm_t9qkh0rsooc8d t446;
                        GM_ST(uint8_t, ((uint8_t *)&t446 + 0), ((uint8_t)0ULL));
                        gm_tg85uffmq8b38 t447;
                        GM_ST(uint8_t, (uint8_t *)&t447 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t447 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t447 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t447 + 0), t444);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t447 + 4), t446);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t447 + 16), t445);
                        void * t448 = gm_l1;
                        gm_tl874t2atnlh3 t449 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t448) + 72));
                        gm_t8658kcqovtpe t450;
                        memset(&t450, 0, sizeof t450);
                        GM_ST(uint8_t, ((uint8_t *)&t450 + 0), ((uint8_t)15ULL));
                        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t450 + 16), t447);
                        gm_b0_1 t451 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t443, &t450, t449);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t452 = gm_l48;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t452);
                        goto gm_fdone;
                    }
                }
gm_lb33:;
                {
                    {
                        {
                            uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t454 = (uint8_t)(t453 == ((uint8_t)15ULL));
                            if (!t454) {
                                goto gm_lb38;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            uint8_t t455 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t456 = (uint8_t)(t455 == ((uint8_t)16ULL));
                            if (!t456) {
                                goto gm_lb39;
                            }
                            goto gm_lb37;
                        }
gm_lb39:;
                        {
                            uint8_t t457 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t458 = (uint8_t)(t457 == ((uint8_t)17ULL));
                            if (!t458) {
                                goto gm_lb40;
                            }
                            goto gm_lb37;
                        }
gm_lb40:;
                        {
                            uint8_t t459 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t460 = (uint8_t)(t459 == ((uint8_t)14ULL));
                            if (!t460) {
                                goto gm_lb41;
                            }
                            goto gm_lb37;
                        }
gm_lb41:;
                        {
                            uint8_t t461 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t462 = (uint8_t)(t461 == ((uint8_t)18ULL));
                            if (!t462) {
                                goto gm_lb42;
                            }
                            goto gm_lb37;
                        }
gm_lb42:;
                        {
                            uint8_t t463 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t464 = (uint8_t)(t463 == ((uint8_t)19ULL));
                            if (!t464) {
                                goto gm_lb43;
                            }
                            goto gm_lb37;
                        }
gm_lb43:;
                        {
                            uint8_t t465 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t466 = (uint8_t)(t465 == ((uint8_t)20ULL));
                            if (!t466) {
                                goto gm_lb44;
                            }
                            goto gm_lb37;
                        }
gm_lb44:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    {
                        {
                            void * t467 = gm_l1;
                            uint8_t t468 = GM_LD(uint8_t, ((((uint8_t *)t467) + 8) + 0));
                            uint8_t t469 = (uint8_t)(t468 == ((uint8_t)19ULL));
                            if (!t469) {
                                goto gm_lb46;
                            }
                            void * t470 = GM_LD(void *, ((((uint8_t *)t467) + 8) + 8));
                            gm_l49 = t470;
                            uint32_t t471 = GM_LD(uint32_t, (((((uint8_t *)t467) + 8) + 8) + 8));
                            gm_l50 = t471;
                            void * t472 = gm_l0;
                            void * t473 = gm_l49;
                            gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t473));
                            uint8_t t475 = gm_f_is_bits_pbijkuf(t472, GM_CONV(gm_csguankko4lmhd, t474));
                            if (!t475) {
                                goto gm_lb46;
                            }
                            {
                                void * t476 = gm_l0;
                                void * t477 = gm_l49;
                                gm_f_expr_tp86bm9((void *)&F.l51, (void *)NULL, cx, NULL, t476, t477);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t478 = gm_l0;
                            void * t479 = gm_l1;
                            gm_t88rcucg1b7uk t480 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t479));
                            uint32_t t481 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t478, t480);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l52 = t481;
                            {
                                void * t482 = gm_l0;
                                uint32_t t483 = gm_l52;
                                uint32_t t484 = gm_l50;
                                gm_t7b7esi3n9r90 t485 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l51));
                                gm_tcpisgbff1dqv t486;
                                GM_ST(uint32_t, (uint8_t *)&t486 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t486 + 8, 0);
                                GM_ST(uint32_t, (uint8_t *)&t486 + 68, 0);
                                GM_ST(uint64_t, (uint8_t *)&t486 + 72, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t486 + 0), t483);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t486 + 16), t485);
                                GM_ST(uint32_t, ((uint8_t *)&t486 + 64), t484);
                                void * t487 = gm_l1;
                                gm_tl874t2atnlh3 t488 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t487) + 72));
                                gm_t8658kcqovtpe t489;
                                memset(&t489, 0, sizeof t489);
                                GM_ST(uint8_t, ((uint8_t *)&t489 + 0), ((uint8_t)21ULL));
                                GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t489 + 16), t486);
                                gm_b0_1 t490 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t482, &t489, t488);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                uint32_t t491 = gm_l52;
                                memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                                GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t491);
                                goto gm_fdone;
                            }
                        }
gm_lb46:;
                    }
gm_lb45:;
                    {
                        void * t492 = gm_l0;
                        void * t493 = gm_l1;
                        gm_f_place_oohr45n((void *)&gm_l146, (void *)NULL, cx, NULL, t492, t493);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t494 = (uint8_t)((((uint8_t *)&gm_l146))[56] != 0);
                    if (t494) {
                        memmove(((uint8_t *)&F.l53), ((uint8_t *)&gm_l146), 56);
                    } else {
                        void * t495 = gm_l0;
                        void * t496 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t495, t496);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t497 = gm_l2;
                        void * t498 = gm_l1;
                        gm_t88rcucg1b7uk t499 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t498));
                        gm_f_layout_a5ovnrm((void *)&gm_l147, (void *)NULL, cx, NULL, t497, t499);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t500 = GM_LD(uint64_t, ((uint8_t *)&gm_l147));
                    uint8_t t501 = ((t500 == ((uint64_t)0ULL)));
                    if (t501) {
                        void * t502 = gm_l1;
                        gm_t88rcucg1b7uk t503 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t502));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t503));
                        goto gm_fdone;
                    }
                    {
                        void * t504 = gm_l0;
                        void * t505 = gm_l1;
                        gm_t88rcucg1b7uk t506 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t505));
                        void * t507 = gm_l1;
                        gm_tl874t2atnlh3 t508 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t507) + 72));
                        gm_tlg23kb3oj23s t509 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l53));
                        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t504, &t509, t506, t508);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb36:;
            }
            {
                {
                    uint8_t t510 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t511 = (uint8_t)(t510 == ((uint8_t)22ULL));
                    if (!t511) {
                        goto gm_lb47;
                    }
                    void * t512 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l54 = t512;
                    {
                        void * t513 = gm_l0;
                        void * t514 = gm_l54;
                        gm_f_operand_place_cdle4ls((void *)&gm_l148, (void *)NULL, cx, NULL, t513, t514);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t515 = (uint8_t)((((uint8_t *)&gm_l148))[56] != 0);
                    if (t515) {
                        memmove(((uint8_t *)&F.l55), ((uint8_t *)&gm_l148), 56);
                    } else {
                        void * t516 = gm_l0;
                        void * t517 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t516, t517);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t518 = gm_l0;
                        void * t519 = gm_l0;
                        gm_tlg23kb3oj23s t520 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l55));
                        uint64_t t521 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t519);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tb31k5l794t7p t522;
                        GM_ST(uint32_t, (uint8_t *)&t522 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t522 + 0), ((uint32_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t522 + 8), t521);
                        gm_tfqmtuprvunod t523;
                        memset(&t523, 0, sizeof t523);
                        GM_ST(uint8_t, ((uint8_t *)&t523 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t523 + 16), t522);
                        gm_tlg23kb3oj23s t524;
                        gm_f_project_6hpd48e((void *)&t524, (void *)NULL, cx, NULL, &t520, &t523);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t525 = gm_l1;
                        gm_t88rcucg1b7uk t526 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t525));
                        void * t527 = gm_l1;
                        gm_tl874t2atnlh3 t528 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t527) + 72));
                        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t518, &t524, t526, t528);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb47:;
                {
                    uint8_t t529 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t530 = (uint8_t)(t529 == ((uint8_t)23ULL));
                    if (!t530) {
                        goto gm_lb48;
                    }
                    void * t531 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l56 = t531;
                    {
                        void * t532 = gm_l0;
                        void * t533 = gm_l56;
                        gm_f_operand_place_cdle4ls((void *)&gm_l149, (void *)NULL, cx, NULL, t532, t533);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t534 = (uint8_t)((((uint8_t *)&gm_l149))[56] != 0);
                    if (t534) {
                        memmove(((uint8_t *)&F.l57), ((uint8_t *)&gm_l149), 56);
                    } else {
                        void * t535 = gm_l0;
                        void * t536 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t535, t536);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t537 = gm_l0;
                        gm_tb31k5l794t7p t538;
                        GM_ST(uint32_t, (uint8_t *)&t538 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t538 + 0), ((uint32_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t538 + 8), ((uint64_t)0ULL));
                        gm_tfqmtuprvunod t539;
                        memset(&t539, 0, sizeof t539);
                        GM_ST(uint8_t, ((uint8_t *)&t539 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t539 + 16), t538);
                        gm_tlg23kb3oj23s t540;
                        gm_f_project_6hpd48e((void *)&t540, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l57)), &t539);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t541 = gm_l1;
                        gm_t88rcucg1b7uk t542 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t541));
                        void * t543 = gm_l1;
                        gm_tl874t2atnlh3 t544 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t543) + 72));
                        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t537, &t540, t542, t544);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb48:;
                {
                    uint8_t t545 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t546 = (uint8_t)(t545 == ((uint8_t)24ULL));
                    if (!t546) {
                        goto gm_lb49;
                    }
                    void * t547 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l58 = t547;
                    {
                        void * t548 = gm_l0;
                        void * t549 = gm_l58;
                        gm_f_place_oohr45n((void *)&gm_l150, (void *)NULL, cx, NULL, t548, t549);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t550 = (uint8_t)((((uint8_t *)&gm_l150))[56] != 0);
                    if (t550) {
                        memmove(((uint8_t *)&F.l59), ((uint8_t *)&gm_l150), 56);
                    } else {
                        void * t551 = gm_l0;
                        void * t552 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t551, t552);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t553 = gm_l0;
                    void * t554 = (void *)((uint8_t *)&F.l59);
                    gm_f_mark_addressed_t8qojin(t553, t554);
                    void * t555 = gm_l0;
                    void * t556 = gm_l1;
                    gm_t88rcucg1b7uk t557 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t556));
                    uint32_t t558 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t555, t557);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l60 = t558;
                    {
                        void * t559 = gm_l0;
                        uint32_t t560 = gm_l60;
                        gm_tlg23kb3oj23s t561 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l59));
                        gm_tlpcrb3vu6o2d t562;
                        GM_ST(uint32_t, (uint8_t *)&t562 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t562 + 0), t560);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t562 + 8), t561);
                        void * t563 = gm_l1;
                        gm_tl874t2atnlh3 t564 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t563) + 72));
                        gm_t8658kcqovtpe t565;
                        memset(&t565, 0, sizeof t565);
                        GM_ST(uint8_t, ((uint8_t *)&t565 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t565 + 16), t562);
                        gm_b0_1 t566 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t559, &t565, t564);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t567 = gm_l60;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t567);
                        goto gm_fdone;
                    }
                }
gm_lb49:;
            }
            {
                {
                    uint8_t t568 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t569 = (uint8_t)(t568 == ((uint8_t)26ULL));
                    if (!t569) {
                        goto gm_lb50;
                    }
                    uint32_t t570 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l61 = t570;
                    void * t571 = gm_l0;
                    void * t572 = gm_l1;
                    gm_t88rcucg1b7uk t573 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t572));
                    uint32_t t574 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t571, t573);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l62 = t574;
                    {
                        void * t575 = gm_l0;
                        uint32_t t576 = gm_l62;
                        uint32_t t577 = gm_l61;
                        void * t578 = gm_l2;
                        void * t579 = (void *)(((uint8_t *)t578) + 8);
                        void * t580 = gm_l1;
                        gm_t88rcucg1b7uk t581 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t580));
                        gm_f_kind_jcnt4hm((void *)&gm_l152, t579, GM_CONV(gm_csguankko4lmhd, t581));
                        {
                            {
                                uint8_t t582 = GM_LD(uint8_t, (((uint8_t *)&gm_l152) + 0));
                                uint8_t t583 = (uint8_t)(t582 == ((uint8_t)15ULL));
                                if (!t583) {
                                    goto gm_lb52;
                                }
                                gm_l151 = ((uint8_t)1);
                                goto gm_lb51;
                            }
gm_lb52:;
                            {
                                gm_l151 = ((uint8_t)0);
                                goto gm_lb51;
                            }
gm_lb53:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 290, 34);
                        }
gm_lb51:;
                        uint8_t t584 = gm_l151;
                        gm_tilqfqdpj446h t585;
                        GM_ST(uint8_t, (uint8_t *)&t585 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t585 + 10, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t585 + 0), t576);
                        GM_ST(uint32_t, ((uint8_t *)&t585 + 4), t577);
                        GM_ST(uint8_t, ((uint8_t *)&t585 + 8), t584);
                        void * t586 = gm_l1;
                        gm_tl874t2atnlh3 t587 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t586) + 72));
                        gm_t8658kcqovtpe t588;
                        memset(&t588, 0, sizeof t588);
                        GM_ST(uint8_t, ((uint8_t *)&t588 + 0), ((uint8_t)16ULL));
                        GM_ST(gm_tilqfqdpj446h, ((uint8_t *)&t588 + 16), t585);
                        gm_b0_1 t589 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t575, &t588, t587);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t590 = gm_l62;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t590);
                        goto gm_fdone;
                    }
                }
gm_lb50:;
                {
                    uint8_t t591 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t592 = (uint8_t)(t591 == ((uint8_t)27ULL));
                    if (!t592) {
                        goto gm_lb54;
                    }
                    uint32_t t593 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l63 = t593;
                    void * t594 = gm_l0;
                    void * t595 = gm_l1;
                    gm_t88rcucg1b7uk t596 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t595));
                    uint32_t t597 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t594, t596);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l64 = t597;
                    {
                        void * t598 = gm_l0;
                        uint32_t t599 = gm_l64;
                        uint32_t t600 = gm_l63;
                        gm_tcc1rq4n1avb2 t601;
                        GM_ST(uint32_t, ((uint8_t *)&t601 + 0), t599);
                        GM_ST(uint32_t, ((uint8_t *)&t601 + 4), t600);
                        void * t602 = gm_l1;
                        gm_tl874t2atnlh3 t603 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t602) + 72));
                        gm_t8658kcqovtpe t604;
                        memset(&t604, 0, sizeof t604);
                        GM_ST(uint8_t, ((uint8_t *)&t604 + 0), ((uint8_t)17ULL));
                        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t604 + 16), t601);
                        gm_b0_1 t605 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t598, &t604, t603);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t606 = gm_l64;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t606);
                        goto gm_fdone;
                    }
                }
gm_lb54:;
                {
                    uint8_t t607 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t608 = (uint8_t)(t607 == ((uint8_t)28ULL));
                    if (!t608) {
                        goto gm_lb55;
                    }
                    uint32_t t609 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l65 = t609;
                    void * t610 = gm_l0;
                    void * t611 = gm_l1;
                    gm_t88rcucg1b7uk t612 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t611));
                    uint32_t t613 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t610, t612);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l66 = t613;
                    {
                        void * t614 = gm_l0;
                        uint32_t t615 = gm_l66;
                        uint32_t t616 = gm_l65;
                        gm_tcc1rq4n1avb2 t617;
                        GM_ST(uint32_t, ((uint8_t *)&t617 + 0), t615);
                        GM_ST(uint32_t, ((uint8_t *)&t617 + 4), t616);
                        void * t618 = gm_l1;
                        gm_tl874t2atnlh3 t619 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t618) + 72));
                        gm_t8658kcqovtpe t620;
                        memset(&t620, 0, sizeof t620);
                        GM_ST(uint8_t, ((uint8_t *)&t620 + 0), ((uint8_t)18ULL));
                        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t620 + 16), t617);
                        gm_b0_1 t621 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t614, &t620, t619);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t622 = gm_l66;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t622);
                        goto gm_fdone;
                    }
                }
gm_lb55:;
            }
            {
                {
                    uint8_t t623 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t624 = (uint8_t)(t623 == ((uint8_t)29ULL));
                    if (!t624) {
                        goto gm_lb56;
                    }
                    gm_t9qkh0rsooc8d t625 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t107) + 8) + 8));
                    gm_l67 = t625;
                    void * t626 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l68 = t626;
                    {
                        void * t627 = gm_l0;
                        void * t628 = gm_l68;
                        gm_f_expr_tp86bm9((void *)&F.l69, (void *)NULL, cx, NULL, t627, t628);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t629 = gm_l0;
                    void * t630 = gm_l1;
                    gm_t88rcucg1b7uk t631 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t630));
                    uint32_t t632 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t629, t631);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l70 = t632;
                    {
                        void * t633 = gm_l0;
                        uint32_t t634 = gm_l70;
                        gm_t9qkh0rsooc8d t635 = gm_l67;
                        gm_t7b7esi3n9r90 t636 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l69));
                        gm_tbto4k5jg9fve t637;
                        GM_ST(uint8_t, (uint8_t *)&t637 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t637 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t637 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t637 + 0), t634);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t637 + 4), t635);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t637 + 16), t636);
                        void * t638 = gm_l1;
                        gm_tl874t2atnlh3 t639 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t638) + 72));
                        gm_t8658kcqovtpe t640;
                        memset(&t640, 0, sizeof t640);
                        GM_ST(uint8_t, ((uint8_t *)&t640 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tbto4k5jg9fve, ((uint8_t *)&t640 + 16), t637);
                        gm_b0_1 t641 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t633, &t640, t639);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t642 = gm_l70;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t642);
                        goto gm_fdone;
                    }
                }
gm_lb56:;
                {
                    uint8_t t643 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t644 = (uint8_t)(t643 == ((uint8_t)30ULL));
                    if (!t644) {
                        goto gm_lb57;
                    }
                    gm_ts6oqppjh0ba3 t645 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t107) + 8) + 8));
                    gm_l71 = t645;
                    void * t646 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l72 = t646;
                    void * t647 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l73 = t647;
                    {
                        void * t648 = gm_l0;
                        void * t649 = gm_l72;
                        gm_f_expr_tp86bm9((void *)&F.l74, (void *)NULL, cx, NULL, t648, t649);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t650 = gm_l0;
                        void * t651 = gm_l73;
                        gm_f_expr_tp86bm9((void *)&F.l75, (void *)NULL, cx, NULL, t650, t651);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t652 = gm_l0;
                    void * t653 = gm_l1;
                    gm_t88rcucg1b7uk t654 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t653));
                    uint32_t t655 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t652, t654);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l76 = t655;
                    {
                        void * t656 = gm_l0;
                        uint32_t t657 = gm_l76;
                        gm_ts6oqppjh0ba3 t658 = gm_l71;
                        gm_t7b7esi3n9r90 t659 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l74));
                        gm_t7b7esi3n9r90 t660 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l75));
                        gm_t1ctgp8j1v2as t661;
                        GM_ST(uint16_t, (uint8_t *)&t661 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t661 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t661 + 0), t657);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t661 + 4), t658);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t661 + 16), t659);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t661 + 64), t660);
                        void * t662 = gm_l1;
                        gm_tl874t2atnlh3 t663 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t662) + 72));
                        gm_t8658kcqovtpe t664;
                        memset(&t664, 0, sizeof t664);
                        GM_ST(uint8_t, ((uint8_t *)&t664 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t664 + 16), t661);
                        gm_b0_1 t665 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t656, &t664, t663);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t666 = gm_l76;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t666);
                        goto gm_fdone;
                    }
                }
gm_lb57:;
                {
                    uint8_t t667 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t668 = (uint8_t)(t667 == ((uint8_t)43ULL));
                    if (!t668) {
                        goto gm_lb58;
                    }
                    void * t669 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l77 = t669;
                    void * t670 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l78 = t670;
                    uint8_t t671 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l79 = t671;
                    {
                        void * t672 = gm_l0;
                        void * t673 = gm_l77;
                        gm_f_expr_tp86bm9((void *)&F.l80, (void *)NULL, cx, NULL, t672, t673);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t674 = gm_l0;
                        void * t675 = gm_l78;
                        gm_f_expr_tp86bm9((void *)&F.l81, (void *)NULL, cx, NULL, t674, t675);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t676 = gm_l0;
                    void * t677 = gm_l1;
                    gm_t88rcucg1b7uk t678 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t677));
                    uint32_t t679 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t676, t678);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l82 = t679;
                    {
                        void * t680 = gm_l0;
                        uint32_t t681 = gm_l82;
                        uint8_t t682 = gm_l79;
                        gm_t7b7esi3n9r90 t683 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l80));
                        gm_t7b7esi3n9r90 t684 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l81));
                        gm_tr6ll1dg9imrl t685;
                        GM_ST(uint8_t, (uint8_t *)&t685 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t685 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t685 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t685 + 0), t681);
                        GM_ST(uint8_t, ((uint8_t *)&t685 + 4), t682);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t685 + 16), t683);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t685 + 64), t684);
                        void * t686 = gm_l1;
                        gm_tl874t2atnlh3 t687 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t686) + 72));
                        gm_t8658kcqovtpe t688;
                        memset(&t688, 0, sizeof t688);
                        GM_ST(uint8_t, ((uint8_t *)&t688 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t688 + 16), t685);
                        gm_b0_1 t689 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t680, &t688, t687);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t690 = gm_l82;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t690);
                        goto gm_fdone;
                    }
                }
gm_lb58:;
            }
            {
                {
                    uint8_t t691 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t692 = (uint8_t)(t691 == ((uint8_t)31ULL));
                    if (!t692) {
                        goto gm_lb59;
                    }
                    uint8_t t693 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l83 = t693;
                    void * t694 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l84 = t694;
                    void * t695 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l85 = t695;
                    void * t696 = gm_l0;
                    void * t697 = gm_l1;
                    uint8_t t698 = gm_l83;
                    void * t699 = gm_l84;
                    void * t700 = gm_l85;
                    gm_f_logic_9o6bp90((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t696, t697, t698, t699, t700);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb59:;
                {
                    uint8_t t701 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t702 = (uint8_t)(t701 == ((uint8_t)32ULL));
                    if (!t702) {
                        goto gm_lb60;
                    }
                    gm_tmn7n0n9raou5 t703 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t107) + 8) + 8));
                    gm_l86 = t703;
                    void * t704 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l87 = t704;
                    gm_tmn7n0n9raou5 t705 = gm_l86;
                    uint8_t t706 = GM_LD(uint8_t, (((uint8_t *)&t705) + 0));
                    uint8_t t707 = (uint8_t)((t706 == ((uint8_t)12ULL)));
                    gm_tmn7n0n9raou5 t708;
                    GM_ST(uint8_t, (uint8_t *)&t708 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t708 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t708 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t708 + 0), ((uint8_t)12ULL));
                    if (t707) {
                        void * t709 = gm_l0;
                        void * t710 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t709, t710);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t711 = gm_l0;
                        void * t712 = gm_l87;
                        gm_f_expr_tp86bm9((void *)&F.l88, (void *)NULL, cx, NULL, t711, t712);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_tmn7n0n9raou5 t713 = gm_l86;
                    uint8_t t714 = GM_LD(uint8_t, (((uint8_t *)&t713) + 0));
                    uint8_t t715 = (uint8_t)((t714 == ((uint8_t)5ULL)));
                    gm_tmn7n0n9raou5 t716;
                    GM_ST(uint8_t, (uint8_t *)&t716 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t716 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t716 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t716 + 0), ((uint8_t)5ULL));
                    if (t715) {
                        void * t717 = gm_l1;
                        gm_t88rcucg1b7uk t718 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t717));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t718));
                        goto gm_fdone;
                    }
                    void * t719 = gm_l0;
                    void * t720 = gm_l1;
                    gm_t88rcucg1b7uk t721 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t720));
                    uint32_t t722 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t719, t721);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l89 = t722;
                    {
                        void * t723 = gm_l0;
                        uint32_t t724 = gm_l89;
                        gm_tmn7n0n9raou5 t725 = gm_l86;
                        gm_t7b7esi3n9r90 t726 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l88));
                        gm_t5hdm76ui2uro t727;
                        GM_ST(uint32_t, (uint8_t *)&t727 + 12, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t727 + 0), t724);
                        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t727 + 4), t725);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t727 + 16), t726);
                        void * t728 = gm_l1;
                        gm_tl874t2atnlh3 t729 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t728) + 72));
                        gm_t8658kcqovtpe t730;
                        memset(&t730, 0, sizeof t730);
                        GM_ST(uint8_t, ((uint8_t *)&t730 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t5hdm76ui2uro, ((uint8_t *)&t730 + 16), t727);
                        gm_b0_1 t731 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t723, &t730, t729);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t732 = gm_l89;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t732);
                        goto gm_fdone;
                    }
                }
gm_lb60:;
                {
                    uint8_t t733 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t734 = (uint8_t)(t733 == ((uint8_t)33ULL));
                    if (!t734) {
                        goto gm_lb61;
                    }
                    gm_t8588ma2cr9d7 t735 = GM_LD(gm_t8588ma2cr9d7, ((((uint8_t *)t107) + 8) + 8));
                    gm_l90 = t735;
                    gm_b16_8 t736 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l91 = t736;
                    {
                        {
                            uint8_t t737 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t738 = (uint8_t)(t737 == ((uint8_t)62ULL));
                            if (!t738) {
                                goto gm_lb63;
                            }
                            void * t739 = gm_l0;
                            void * t740 = gm_l1;
                            gm_b16_8 t741 = gm_l91;
                            gm_b0_1 t742 = gm_f_assert__nol4kp3((void *)NULL, cx, NULL, t739, t740, t741);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t743 = gm_l1;
                            gm_t88rcucg1b7uk t744 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t743));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t744));
                            goto gm_fdone;
                        }
gm_lb63:;
                        {
                            uint8_t t745 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t746 = (uint8_t)(t745 == ((uint8_t)81ULL));
                            if (!t746) {
                                goto gm_lb64;
                            }
                            void * t747 = gm_l1;
                            gm_t88rcucg1b7uk t748 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t747));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t748));
                            goto gm_fdone;
                        }
gm_lb64:;
                        {
                            {
                                {
                                    uint8_t t749 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t750 = (uint8_t)(t749 == ((uint8_t)36ULL));
                                    if (!t750) {
                                        goto gm_lb67;
                                    }
                                    goto gm_lb66;
                                }
gm_lb67:;
                                {
                                    uint8_t t751 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t752 = (uint8_t)(t751 == ((uint8_t)37ULL));
                                    if (!t752) {
                                        goto gm_lb68;
                                    }
                                    goto gm_lb66;
                                }
gm_lb68:;
                                {
                                    uint8_t t753 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t754 = (uint8_t)(t753 == ((uint8_t)38ULL));
                                    if (!t754) {
                                        goto gm_lb69;
                                    }
                                    goto gm_lb66;
                                }
gm_lb69:;
                                {
                                    uint8_t t755 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t756 = (uint8_t)(t755 == ((uint8_t)39ULL));
                                    if (!t756) {
                                        goto gm_lb70;
                                    }
                                    goto gm_lb66;
                                }
gm_lb70:;
                                goto gm_lb65;
                            }
gm_lb66:;
                            void * t757 = gm_l0;
                            void * t758 = gm_l1;
                            gm_t8588ma2cr9d7 t759 = gm_l90;
                            gm_b16_8 t760 = gm_l91;
                            gm_f_get_d0bmskj((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t757, t758, t759, t760);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb65:;
                        {
                            goto gm_lb62;
                        }
gm_lb71:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 333, 13);
                    }
gm_lb62:;
                    void * t761 = gm_l0;
                    gm_b16_8 t762 = gm_l91;
                    gm_b16_8 t763 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t761, t762);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l92 = t763;
                    {
                        void * t764 = gm_l2;
                        void * t765 = gm_l1;
                        gm_t88rcucg1b7uk t766 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t765));
                        gm_f_layout_a5ovnrm((void *)&gm_l154, (void *)NULL, cx, NULL, t764, t766);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t767 = GM_LD(uint64_t, ((uint8_t *)&gm_l154));
                    uint8_t t768 = ((t767 == ((uint64_t)0ULL)));
                    if (t768) {
                        gm_l153 = ((uint8_t)1);
                    } else {
                        void * t769 = gm_l0;
                        void * t770 = gm_l1;
                        uint8_t t771 = gm_f_diverges_p3okmol(t769, t770);
                        gm_l153 = t771;
                    }
                    uint8_t t772 = gm_l153;
                    if (t772) {
                        {
                            void * t773 = gm_l0;
                            gm_t9805qtonbsag t774;
                            memset(&t774, 0, sizeof t774);
                            gm_t8588ma2cr9d7 t775 = gm_l90;
                            gm_b16_8 t776 = gm_l92;
                            gm_te4bkgqfi01mk t777;
                            GM_ST(uint32_t, (uint8_t *)&t777 + 20, 0);
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t777 + 0), t774);
                            GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t777 + 8), t775);
                            GM_ST(gm_b16_8, ((uint8_t *)&t777 + 24), t776);
                            void * t778 = gm_l1;
                            gm_tl874t2atnlh3 t779 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t778) + 72));
                            gm_t8658kcqovtpe t780;
                            memset(&t780, 0, sizeof t780);
                            GM_ST(uint8_t, ((uint8_t *)&t780 + 0), ((uint8_t)7ULL));
                            GM_ST(gm_te4bkgqfi01mk, ((uint8_t *)&t780 + 16), t777);
                            gm_b0_1 t781 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t773, &t780, t779);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t782 = gm_l1;
                            gm_t88rcucg1b7uk t783 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t782));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t783));
                            goto gm_fdone;
                        }
                    }
                    void * t784 = gm_l0;
                    void * t785 = gm_l1;
                    gm_t88rcucg1b7uk t786 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t785));
                    uint32_t t787 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t784, t786);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l93 = t787;
                    {
                        void * t788 = gm_l0;
                        uint32_t t789 = gm_l93;
                        gm_t9805qtonbsag t790;
                        GM_ST(uint8_t, (uint8_t *)&t790 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t790 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t790), t789);
                        ((uint8_t *)&t790)[4] = 1;
                        gm_t8588ma2cr9d7 t791 = gm_l90;
                        gm_b16_8 t792 = gm_l92;
                        gm_te4bkgqfi01mk t793;
                        GM_ST(uint32_t, (uint8_t *)&t793 + 20, 0);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t793 + 0), t790);
                        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t793 + 8), t791);
                        GM_ST(gm_b16_8, ((uint8_t *)&t793 + 24), t792);
                        void * t794 = gm_l1;
                        gm_tl874t2atnlh3 t795 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t794) + 72));
                        gm_t8658kcqovtpe t796;
                        memset(&t796, 0, sizeof t796);
                        GM_ST(uint8_t, ((uint8_t *)&t796 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_te4bkgqfi01mk, ((uint8_t *)&t796 + 16), t793);
                        gm_b0_1 t797 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t788, &t796, t795);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t798 = gm_l93;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t798);
                        goto gm_fdone;
                    }
                }
gm_lb61:;
            }
            {
                {
                    uint8_t t799 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t800 = (uint8_t)(t799 == ((uint8_t)34ULL));
                    if (!t800) {
                        goto gm_lb72;
                    }
                    {
                        memmove(((uint8_t *)&F.l94), ((((uint8_t *)t107) + 8) + 8), 40);
                    }
                    gm_b16_8 t801 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 40));
                    gm_l95 = t801;
                    {
                        {
                            uint8_t t802 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t803 = (uint8_t)(t802 == ((uint8_t)0ULL));
                            if (!t803) {
                                goto gm_lb74;
                            }
                            uint32_t t804 = GM_LD(uint32_t, (((uint8_t *)&F.l94) + 8));
                            gm_l96 = t804;
                            uint32_t t805 = gm_l96;
                            memset(&F.l100, 0, sizeof F.l100);
                            GM_ST(uint8_t, ((uint8_t *)&F.l100 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&F.l100 + 16), t805);
                            goto gm_lb73;
                        }
gm_lb74:;
                        {
                            uint8_t t806 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t807 = (uint8_t)(t806 == ((uint8_t)1ULL));
                            if (!t807) {
                                goto gm_lb75;
                            }
                            {
                                memmove(((uint8_t *)&gm_l97), (((uint8_t *)&F.l94) + 8), 24);
                            }
                            uint32_t t808 = GM_LD(uint32_t, ((((uint8_t *)&F.l94) + 8) + 24));
                            gm_l98 = t808;
                            {
                                uint32_t t809 = gm_l98;
                                gm_tiilmfd16v3fc t810 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l97));
                                gm_t419mo45g7j0a t811;
                                GM_ST(uint32_t, (uint8_t *)&t811 + 28, 0);
                                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t811 + 0), t810);
                                GM_ST(uint32_t, ((uint8_t *)&t811 + 24), t809);
                                GM_ST(uint8_t, (uint8_t *)&F.l100 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l100 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l100 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l100 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l100 + 48, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l100 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&F.l100 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&F.l100 + 16), t811);
                            }
                            goto gm_lb73;
                        }
gm_lb75:;
                        {
                            uint8_t t812 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t813 = (uint8_t)(t812 == ((uint8_t)2ULL));
                            if (!t813) {
                                goto gm_lb76;
                            }
                            void * t814 = GM_LD(void *, (((uint8_t *)&F.l94) + 8));
                            gm_l99 = t814;
                            {
                                void * t815 = gm_l0;
                                void * t816 = gm_l99;
                                gm_t7b7esi3n9r90 t817;
                                gm_f_expr_tp86bm9((void *)&t817, (void *)NULL, cx, NULL, t815, t816);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.l100 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l100 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l100 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l100 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&F.l100 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l100 + 16), t817);
                            }
                            goto gm_lb73;
                        }
gm_lb76:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 352, 25);
                    }
gm_lb73:;
                    void * t818 = gm_l0;
                    gm_b16_8 t819 = gm_l95;
                    gm_b16_8 t820 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t818, t819);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l101 = t820;
                    {
                        void * t821 = gm_l2;
                        void * t822 = gm_l1;
                        gm_t88rcucg1b7uk t823 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t822));
                        gm_f_layout_a5ovnrm((void *)&gm_l156, (void *)NULL, cx, NULL, t821, t823);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t824 = GM_LD(uint64_t, ((uint8_t *)&gm_l156));
                    uint8_t t825 = ((t824 == ((uint64_t)0ULL)));
                    if (t825) {
                        gm_l155 = ((uint8_t)1);
                    } else {
                        void * t826 = gm_l0;
                        void * t827 = gm_l1;
                        uint8_t t828 = gm_f_diverges_p3okmol(t826, t827);
                        gm_l155 = t828;
                    }
                    uint8_t t829 = gm_l155;
                    if (t829) {
                        {
                            void * t830 = gm_l0;
                            gm_t9805qtonbsag t831;
                            memset(&t831, 0, sizeof t831);
                            gm_b16_8 t832 = gm_l101;
                            gm_tllthq21h2f0k t833 = GM_LD(gm_tllthq21h2f0k, ((uint8_t *)&F.l100));
                            gm_t116nudii5c54 t834;
                            GM_ST(uint64_t, (uint8_t *)&t834 + 8, 0);
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t834 + 0), t831);
                            GM_ST(gm_tllthq21h2f0k, ((uint8_t *)&t834 + 16), t833);
                            GM_ST(gm_b16_8, ((uint8_t *)&t834 + 80), t832);
                            void * t835 = gm_l1;
                            gm_tl874t2atnlh3 t836 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t835) + 72));
                            gm_t8658kcqovtpe t837;
                            memset(&t837, 0, sizeof t837);
                            GM_ST(uint8_t, ((uint8_t *)&t837 + 0), ((uint8_t)8ULL));
                            GM_ST(gm_t116nudii5c54, ((uint8_t *)&t837 + 16), t834);
                            gm_b0_1 t838 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t830, &t837, t836);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t839 = gm_l0;
                        void * t840 = gm_l1;
                        uint8_t t841 = gm_f_diverges_p3okmol(t839, t840);
                        if (t841) {
                            void * t842 = gm_l0;
                            void * t843 = gm_l1;
                            gm_tl874t2atnlh3 t844 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t843) + 72));
                            gm_t8658kcqovtpe t845;
                            memset(&t845, 0, sizeof t845);
                            GM_ST(uint8_t, ((uint8_t *)&t845 + 0), ((uint8_t)43ULL));
                            gm_b0_1 t846 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t842, &t845, t844);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t847 = gm_l1;
                            gm_t88rcucg1b7uk t848 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t847));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t848));
                            goto gm_fdone;
                        }
                    }
                    void * t849 = gm_l0;
                    void * t850 = gm_l1;
                    gm_t88rcucg1b7uk t851 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t850));
                    uint32_t t852 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t849, t851);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l102 = t852;
                    {
                        void * t853 = gm_l0;
                        uint32_t t854 = gm_l102;
                        gm_t9805qtonbsag t855;
                        GM_ST(uint8_t, (uint8_t *)&t855 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t855 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t855), t854);
                        ((uint8_t *)&t855)[4] = 1;
                        gm_b16_8 t856 = gm_l101;
                        gm_tllthq21h2f0k t857 = GM_LD(gm_tllthq21h2f0k, ((uint8_t *)&F.l100));
                        gm_t116nudii5c54 t858;
                        GM_ST(uint64_t, (uint8_t *)&t858 + 8, 0);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t858 + 0), t855);
                        GM_ST(gm_tllthq21h2f0k, ((uint8_t *)&t858 + 16), t857);
                        GM_ST(gm_b16_8, ((uint8_t *)&t858 + 80), t856);
                        void * t859 = gm_l1;
                        gm_tl874t2atnlh3 t860 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t859) + 72));
                        gm_t8658kcqovtpe t861;
                        memset(&t861, 0, sizeof t861);
                        GM_ST(uint8_t, ((uint8_t *)&t861 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_t116nudii5c54, ((uint8_t *)&t861 + 16), t858);
                        gm_b0_1 t862 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t853, &t861, t860);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t863 = gm_l102;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t863);
                        goto gm_fdone;
                    }
                }
gm_lb72:;
            }
            {
                {
                    {
                        {
                            uint8_t t864 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t865 = (uint8_t)(t864 == ((uint8_t)35ULL));
                            if (!t865) {
                                goto gm_lb79;
                            }
                            gm_b16_8 t866 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l103 = t866;
                            goto gm_lb78;
                        }
gm_lb79:;
                        {
                            uint8_t t867 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t868 = (uint8_t)(t867 == ((uint8_t)36ULL));
                            if (!t868) {
                                goto gm_lb80;
                            }
                            gm_b16_8 t869 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l103 = t869;
                            goto gm_lb78;
                        }
gm_lb80:;
                        goto gm_lb77;
                    }
gm_lb78:;
                    void * t870 = gm_l0;
                    gm_b16_8 t871 = gm_l103;
                    gm_b16_8 t872 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t870, t871);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l105 = t872;
                    void * t873 = gm_l0;
                    void * t874 = gm_l1;
                    gm_t88rcucg1b7uk t875 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t874));
                    uint32_t t876 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t873, t875);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l106 = t876;
                    {
                        void * t877 = gm_l0;
                        uint32_t t878 = gm_l106;
                        gm_b16_8 t879 = gm_l105;
                        gm_tiilmfd16v3fc t880;
                        GM_ST(uint32_t, (uint8_t *)&t880 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t880 + 0), t878);
                        GM_ST(gm_b16_8, ((uint8_t *)&t880 + 8), t879);
                        void * t881 = gm_l1;
                        gm_tl874t2atnlh3 t882 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t881) + 72));
                        gm_t8658kcqovtpe t883;
                        memset(&t883, 0, sizeof t883);
                        GM_ST(uint8_t, ((uint8_t *)&t883 + 0), ((uint8_t)9ULL));
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t883 + 16), t880);
                        gm_b0_1 t884 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t877, &t883, t882);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t885 = gm_l106;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t885);
                        goto gm_fdone;
                    }
                }
gm_lb77:;
                {
                    uint8_t t886 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t887 = (uint8_t)(t886 == ((uint8_t)38ULL));
                    if (!t887) {
                        goto gm_lb81;
                    }
                    gm_ttggq7ng4kp3p t888 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t107) + 8) + 8));
                    gm_l107 = t888;
                    {
                        memset(&F.l108, 0, sizeof F.l108);
                    }
                    {
                        {
                            uint8_t t889 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l107) + 8)) != 0);
                            if (!t889) {
                                goto gm_lb83;
                            }
                            void * t890 = GM_LD(void *, (((uint8_t *)&gm_l107) + 8));
                            gm_l109 = t890;
                            {
                                void * t891 = gm_l0;
                                void * t892 = gm_l109;
                                gm_t7b7esi3n9r90 t893;
                                gm_f_expr_tp86bm9((void *)&t893, (void *)NULL, cx, NULL, t891, t892);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.l108 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l108 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l108 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l108 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l108), t893);
                                ((uint8_t *)&F.l108)[48] = 1;
                            }
                            goto gm_lb82;
                        }
gm_lb83:;
                    }
gm_lb82:;
                    void * t894 = gm_l0;
                    void * t895 = gm_l1;
                    gm_t88rcucg1b7uk t896 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t895));
                    uint32_t t897 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t894, t896);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l110 = t897;
                    {
                        void * t898 = gm_l0;
                        uint32_t t899 = gm_l110;
                        uint32_t t900 = GM_LD(uint32_t, ((uint8_t *)&gm_l107));
                        gm_tf8kfhcnval78 t901 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l108));
                        gm_tvv8lnu6a3ppj t902;
                        GM_ST(uint64_t, (uint8_t *)&t902 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t902 + 0), t899);
                        GM_ST(uint32_t, ((uint8_t *)&t902 + 4), t900);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t902 + 16), t901);
                        void * t903 = gm_l1;
                        gm_tl874t2atnlh3 t904 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t903) + 72));
                        gm_t8658kcqovtpe t905;
                        memset(&t905, 0, sizeof t905);
                        GM_ST(uint8_t, ((uint8_t *)&t905 + 0), ((uint8_t)14ULL));
                        GM_ST(gm_tvv8lnu6a3ppj, ((uint8_t *)&t905 + 16), t902);
                        gm_b0_1 t906 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t898, &t905, t904);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t907 = gm_l110;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t907);
                        goto gm_fdone;
                    }
                }
gm_lb81:;
            }
            {
                {
                    uint8_t t908 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t909 = (uint8_t)(t908 == ((uint8_t)39ULL));
                    if (!t909) {
                        goto gm_lb84;
                    }
                    void * t910 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l111 = t910;
                    {
                        void * t911 = gm_l0;
                        void * t912 = gm_l111;
                        gm_f_expr_tp86bm9((void *)&F.l112, (void *)NULL, cx, NULL, t911, t912);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t913 = gm_l0;
                    void * t914 = gm_l1;
                    gm_t88rcucg1b7uk t915 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t914));
                    uint32_t t916 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t913, t915);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l113 = t916;
                    {
                        void * t917 = gm_l0;
                        uint32_t t918 = gm_l113;
                        gm_t7b7esi3n9r90 t919 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l112));
                        gm_tf8kfhcnval78 t920;
                        GM_ST(uint8_t, (uint8_t *)&t920 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t920 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t920 + 52, 0);
                        GM_ST(uint64_t, (uint8_t *)&t920 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t920), t919);
                        ((uint8_t *)&t920)[48] = 1;
                        gm_t9qkh0rsooc8d t921;
                        GM_ST(uint8_t, ((uint8_t *)&t921 + 0), ((uint8_t)3ULL));
                        gm_tg85uffmq8b38 t922;
                        GM_ST(uint8_t, (uint8_t *)&t922 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t922 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t922 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t922 + 0), t918);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t922 + 4), t921);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t922 + 16), t920);
                        void * t923 = gm_l1;
                        gm_tl874t2atnlh3 t924 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t923) + 72));
                        gm_t8658kcqovtpe t925;
                        memset(&t925, 0, sizeof t925);
                        GM_ST(uint8_t, ((uint8_t *)&t925 + 0), ((uint8_t)15ULL));
                        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t925 + 16), t922);
                        gm_b0_1 t926 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t917, &t925, t924);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t927 = gm_l113;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t927);
                        goto gm_fdone;
                    }
                }
gm_lb84:;
                {
                    uint8_t t928 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t929 = (uint8_t)(t928 == ((uint8_t)46ULL));
                    if (!t929) {
                        goto gm_lb85;
                    }
                    void * t930 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l114 = t930;
                    void * t931 = gm_l0;
                    void * t932 = gm_l114;
                    void * t933 = gm_l1;
                    gm_t88rcucg1b7uk t934 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t933));
                    gm_f_block_iujbbjb((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t931, t932, t934);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb85:;
                {
                    uint8_t t935 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t936 = (uint8_t)(t935 == ((uint8_t)47ULL));
                    if (!t936) {
                        goto gm_lb86;
                    }
                    {
                        memmove(((uint8_t *)&gm_l115), ((((uint8_t *)t107) + 8) + 8), 32);
                    }
                    {
                        void * t937 = gm_l0;
                        void * t938 = gm_l1;
                        gm_trl2khmuphncd t939 = GM_LD(gm_trl2khmuphncd, ((uint8_t *)&gm_l115));
                        gm_f_if_expr_bsdi44h((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t937, t938, &t939);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb86:;
                {
                    uint8_t t940 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t941 = (uint8_t)(t940 == ((uint8_t)48ULL));
                    if (!t941) {
                        goto gm_lb87;
                    }
                    {
                        memmove(((uint8_t *)&F.l116), ((((uint8_t *)t107) + 8) + 8), 40);
                    }
                    {
                        void * t942 = gm_l0;
                        void * t943 = gm_l1;
                        gm_t710lsrq1m8gk t944 = GM_LD(gm_t710lsrq1m8gk, ((uint8_t *)&F.l116));
                        gm_f_loop_expr_k6v92f2((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t942, t943, &t944);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb87:;
                {
                    uint8_t t945 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t946 = (uint8_t)(t945 == ((uint8_t)49ULL));
                    if (!t946) {
                        goto gm_lb88;
                    }
                    {
                        memmove(((uint8_t *)&gm_l117), ((((uint8_t *)t107) + 8) + 8), 24);
                    }
                    {
                        void * t947 = gm_l0;
                        gm_te9hrk0u35rev t948 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l117));
                        gm_f_local_target_vvq8lnf((void *)&gm_l157, t947, t948);
                    }
                    uint8_t t949 = (uint8_t)((((uint8_t *)&gm_l157))[20] != 0);
                    if (t949) {
                        memmove(((uint8_t *)&gm_l121), ((uint8_t *)&gm_l157), 20);
                    } else {
                        {
                            memset(&F.l118, 0, sizeof F.l118);
                        }
                        {
                            {
                                uint8_t t950 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l117) + 16)) != 0);
                                if (!t950) {
                                    goto gm_lb90;
                                }
                                void * t951 = GM_LD(void *, (((uint8_t *)&gm_l117) + 16));
                                gm_l119 = t951;
                                void * t952 = gm_l0;
                                void * t953 = gm_l119;
                                gm_f_expr_tp86bm9((void *)&F.l120, (void *)NULL, cx, NULL, t952, t953);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        {
                                            void * t954 = gm_l2;
                                            void * t955 = gm_l119;
                                            gm_t88rcucg1b7uk t956 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t955));
                                            gm_f_layout_a5ovnrm((void *)&gm_l158, (void *)NULL, cx, NULL, t954, t956);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        uint64_t t957 = GM_LD(uint64_t, ((uint8_t *)&gm_l158));
                                        uint8_t t958 = (t957 > ((uint64_t)0ULL));
                                        if (!t958) {
                                            goto gm_lb92;
                                        }
                                        void * t959 = gm_l0;
                                        void * t960 = gm_l119;
                                        uint8_t t961 = gm_f_diverges_p3okmol(t959, t960);
                                        uint8_t t962 = (uint8_t)!t961;
                                        if (!t962) {
                                            goto gm_lb92;
                                        }
                                        {
                                            gm_t7b7esi3n9r90 t963 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l120));
                                            GM_ST(uint8_t, (uint8_t *)&F.l118 + 49, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l118 + 50, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l118 + 52, 0);
                                            GM_ST(uint64_t, (uint8_t *)&F.l118 + 56, 0);
                                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l118), t963);
                                            ((uint8_t *)&F.l118)[48] = 1;
                                        }
                                        goto gm_lb91;
                                    }
gm_lb92:;
                                }
gm_lb91:;
                                goto gm_lb89;
                            }
gm_lb90:;
                        }
gm_lb89:;
                        {
                            void * t964 = gm_l0;
                            gm_te9hrk0u35rev t965 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l117));
                            gm_t7pnmbsgd9mnq t966;
                            GM_ST(uint8_t, (uint8_t *)&t966 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t966 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t966 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t966 + 4), t965);
                            gm_tf8kfhcnval78 t967 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l118));
                            gm_t7dcjsiqi95h3 t968;
                            GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t968 + 0), t966);
                            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t968 + 16), t967);
                            void * t969 = gm_l1;
                            gm_tl874t2atnlh3 t970 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t969) + 72));
                            gm_t8658kcqovtpe t971;
                            memset(&t971, 0, sizeof t971);
                            GM_ST(uint8_t, ((uint8_t *)&t971 + 0), ((uint8_t)39ULL));
                            GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t971 + 16), t968);
                            gm_b0_1 t972 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t964, &t971, t970);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t973 = gm_l1;
                            gm_t88rcucg1b7uk t974 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t973));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t974));
                            goto gm_fdone;
                        }
                    }
                    {
                        memmove(((uint8_t *)&gm_l121), ((uint8_t *)&gm_l121), 20);
                    }
                    uint32_t t975 = GM_LD(uint32_t, ((uint8_t *)&gm_l121));
                    gm_l122 = t975;
                    {
                        {
                            uint8_t t976 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l117) + 16)) != 0);
                            if (!t976) {
                                goto gm_lb94;
                            }
                            void * t977 = GM_LD(void *, (((uint8_t *)&gm_l117) + 16));
                            gm_l123 = t977;
                            void * t978 = gm_l0;
                            void * t979 = gm_l123;
                            gm_f_expr_tp86bm9((void *)&F.l124, (void *)NULL, cx, NULL, t978, t979);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    void * t980 = gm_l0;
                                    uint32_t t981 = gm_l122;
                                    gm_t9805qtonbsag t982 = gm_f_result_of_9m0s9vd(t980, t981);
                                    gm_l159 = t982;
                                    uint8_t t983 = (uint8_t)((((uint8_t *)&gm_l159))[4] != 0);
                                    if (!t983) {
                                        goto gm_lb96;
                                    }
                                    uint32_t t984 = GM_LD(uint32_t, ((uint8_t *)&gm_l159));
                                    gm_l125 = t984;
                                    {
                                        void * t985 = gm_l0;
                                        uint32_t t986 = gm_l125;
                                        gm_tlg23kb3oj23s t987;
                                        gm_f_local_place_qnf3mb2((void *)&t987, t986);
                                        gm_t7b7esi3n9r90 t988 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l124));
                                        gm_t6fp3q71vjrmk t989;
                                        GM_ST(uint64_t, (uint8_t *)&t989 + 56, 0);
                                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t989 + 0), t987);
                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t989 + 64), t988);
                                        void * t990 = gm_l1;
                                        gm_tl874t2atnlh3 t991 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t990) + 72));
                                        gm_t8658kcqovtpe t992;
                                        memset(&t992, 0, sizeof t992);
                                        GM_ST(uint8_t, ((uint8_t *)&t992 + 0), ((uint8_t)25ULL));
                                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t992 + 16), t989);
                                        gm_b0_1 t993 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t985, &t992, t991);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb95;
                                }
gm_lb96:;
                            }
gm_lb95:;
                            goto gm_lb93;
                        }
gm_lb94:;
                    }
gm_lb93:;
                    {
                        void * t994 = gm_l0;
                        uint32_t t995 = GM_LD(uint32_t, (((uint8_t *)&gm_l121) + 4));
                        void * t996 = gm_l1;
                        gm_tl874t2atnlh3 t997 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t996) + 72));
                        gm_t8658kcqovtpe t998;
                        memset(&t998, 0, sizeof t998);
                        GM_ST(uint8_t, ((uint8_t *)&t998 + 0), ((uint8_t)30ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t998 + 16), t995);
                        gm_b0_1 t999 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t994, &t998, t997);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t1000 = gm_l1;
                        gm_t88rcucg1b7uk t1001 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1000));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1001));
                        goto gm_fdone;
                    }
                }
gm_lb88:;
            }
            {
                {
                    uint8_t t1002 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1003 = (uint8_t)(t1002 == ((uint8_t)50ULL));
                    if (!t1003) {
                        goto gm_lb97;
                    }
                    gm_te9hrk0u35rev t1004 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t107) + 8) + 8));
                    gm_l126 = t1004;
                    {
                        void * t1005 = gm_l0;
                        gm_te9hrk0u35rev t1006 = gm_l126;
                        gm_f_local_target_vvq8lnf((void *)&gm_l160, t1005, t1006);
                    }
                    uint8_t t1007 = (uint8_t)((((uint8_t *)&gm_l160))[20] != 0);
                    if (t1007) {
                        memmove(((uint8_t *)&gm_l127), ((uint8_t *)&gm_l160), 20);
                    } else {
                        {
                            void * t1008 = gm_l0;
                            gm_te9hrk0u35rev t1009 = gm_l126;
                            gm_tf8kfhcnval78 t1010;
                            memset(&t1010, 0, sizeof t1010);
                            gm_t7pnmbsgd9mnq t1011;
                            GM_ST(uint8_t, (uint8_t *)&t1011 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1011 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t1011 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t1011 + 4), t1009);
                            gm_t7dcjsiqi95h3 t1012;
                            GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t1012 + 0), t1011);
                            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1012 + 16), t1010);
                            void * t1013 = gm_l1;
                            gm_tl874t2atnlh3 t1014 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1013) + 72));
                            gm_t8658kcqovtpe t1015;
                            memset(&t1015, 0, sizeof t1015);
                            GM_ST(uint8_t, ((uint8_t *)&t1015 + 0), ((uint8_t)39ULL));
                            GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t1015 + 16), t1012);
                            gm_b0_1 t1016 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1008, &t1015, t1014);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t1017 = gm_l1;
                            gm_t88rcucg1b7uk t1018 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1017));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1018));
                            goto gm_fdone;
                        }
                    }
                    {
                        void * t1019 = gm_l0;
                        uint32_t t1020 = GM_LD(uint32_t, (((uint8_t *)&gm_l127) + 8));
                        void * t1021 = gm_l1;
                        gm_tl874t2atnlh3 t1022 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1021) + 72));
                        gm_t8658kcqovtpe t1023;
                        memset(&t1023, 0, sizeof t1023);
                        GM_ST(uint8_t, ((uint8_t *)&t1023 + 0), ((uint8_t)30ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t1023 + 16), t1020);
                        gm_b0_1 t1024 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1019, &t1023, t1022);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t1025 = gm_l1;
                        gm_t88rcucg1b7uk t1026 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1025));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1026));
                        goto gm_fdone;
                    }
                }
gm_lb97:;
                {
                    uint8_t t1027 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1028 = (uint8_t)(t1027 == ((uint8_t)51ULL));
                    if (!t1028) {
                        goto gm_lb98;
                    }
                    uint32_t t1029 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l128 = t1029;
                    void * t1030 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l129 = t1030;
                    {
                        void * t1031 = gm_l0;
                        void * t1032 = gm_l129;
                        gm_f_expr_tp86bm9((void *)&F.l130, (void *)NULL, cx, NULL, t1031, t1032);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint32_t t1033 = gm_l128;
                    uint8_t t1034 = (!(t1033 == ((uint32_t)0ULL)));
                    if (t1034) {
                        {
                            {
                                {
                                    void * t1035 = gm_l2;
                                    void * t1036 = gm_l129;
                                    gm_t88rcucg1b7uk t1037 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1036));
                                    gm_f_layout_a5ovnrm((void *)&gm_l161, (void *)NULL, cx, NULL, t1035, t1037);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint64_t t1038 = GM_LD(uint64_t, ((uint8_t *)&gm_l161));
                                uint8_t t1039 = (t1038 > ((uint64_t)0ULL));
                                if (!t1039) {
                                    goto gm_lb100;
                                }
                                void * t1040 = gm_l0;
                                void * t1041 = gm_l129;
                                uint8_t t1042 = gm_f_diverges_p3okmol(t1040, t1041);
                                uint8_t t1043 = (uint8_t)!t1042;
                                if (!t1043) {
                                    goto gm_lb100;
                                }
                                {
                                    gm_t7b7esi3n9r90 t1044 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l130));
                                    GM_ST(uint8_t, (uint8_t *)&F.l131 + 49, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.l131 + 50, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.l131 + 52, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l131 + 56, 0);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l131), t1044);
                                    ((uint8_t *)&F.l131)[48] = 1;
                                }
                                goto gm_lb99;
                            }
gm_lb100:;
                            memset(&F.l131, 0, sizeof F.l131);
                        }
gm_lb99:;
                        {
                            void * t1045 = gm_l0;
                            uint32_t t1046 = gm_l128;
                            gm_t7pnmbsgd9mnq t1047;
                            GM_ST(uint8_t, (uint8_t *)&t1047 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1047 + 2, 0);
                            GM_ST(uint64_t, (uint8_t *)&t1047 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t1047 + 0), ((uint8_t)2ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t1047 + 4), t1046);
                            gm_tf8kfhcnval78 t1048 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l131));
                            gm_t7dcjsiqi95h3 t1049;
                            GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t1049 + 0), t1047);
                            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1049 + 16), t1048);
                            void * t1050 = gm_l1;
                            gm_tl874t2atnlh3 t1051 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1050) + 72));
                            gm_t8658kcqovtpe t1052;
                            memset(&t1052, 0, sizeof t1052);
                            GM_ST(uint8_t, ((uint8_t *)&t1052 + 0), ((uint8_t)39ULL));
                            GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t1052 + 16), t1049);
                            gm_b0_1 t1053 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1045, &t1052, t1051);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t1054 = gm_l1;
                            gm_t88rcucg1b7uk t1055 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1054));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1055));
                            goto gm_fdone;
                        }
                    }
                    {
                        void * t1056 = gm_l0;
                        gm_t7b7esi3n9r90 t1057 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l130));
                        gm_tf8kfhcnval78 t1058;
                        GM_ST(uint8_t, (uint8_t *)&t1058 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1058 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1058 + 52, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1058 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1058), t1057);
                        ((uint8_t *)&t1058)[48] = 1;
                        void * t1059 = gm_l1;
                        gm_tl874t2atnlh3 t1060 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1059) + 72));
                        gm_t8658kcqovtpe t1061;
                        memset(&t1061, 0, sizeof t1061);
                        GM_ST(uint8_t, ((uint8_t *)&t1061 + 0), ((uint8_t)32ULL));
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1061 + 16), t1058);
                        gm_b0_1 t1062 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1056, &t1061, t1060);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t1063 = gm_l1;
                        gm_t88rcucg1b7uk t1064 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1063));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1064));
                        goto gm_fdone;
                    }
                }
gm_lb98:;
            }
            {
                {
                    uint8_t t1065 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1066 = (uint8_t)(t1065 == ((uint8_t)52ULL));
                    if (!t1066) {
                        goto gm_lb101;
                    }
                    void * t1067 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l132 = t1067;
                    {
                        void * t1068 = gm_l0;
                        void * t1069 = gm_l132;
                        gm_f_expr_tp86bm9((void *)&F.l133, (void *)NULL, cx, NULL, t1068, t1069);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t1070 = gm_l0;
                        gm_t7b7esi3n9r90 t1071 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l133));
                        void * t1072 = gm_l1;
                        gm_tl874t2atnlh3 t1073 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1072) + 72));
                        gm_t8658kcqovtpe t1074;
                        memset(&t1074, 0, sizeof t1074);
                        GM_ST(uint8_t, ((uint8_t *)&t1074 + 0), ((uint8_t)34ULL));
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1074 + 16), t1071);
                        gm_b0_1 t1075 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1070, &t1074, t1073);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t1076 = gm_l1;
                        gm_t88rcucg1b7uk t1077 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1076));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1077));
                        goto gm_fdone;
                    }
                }
gm_lb101:;
                {
                    uint8_t t1078 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1079 = (uint8_t)(t1078 == ((uint8_t)44ULL));
                    if (!t1079) {
                        goto gm_lb102;
                    }
                    void * t1080 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l134 = t1080;
                    gm_b16_8 t1081 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l135 = t1081;
                    void * t1082 = gm_l0;
                    void * t1083 = gm_l1;
                    void * t1084 = gm_l134;
                    gm_b16_8 t1085 = gm_l135;
                    gm_f_match_expr_rfas5u2((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t1082, t1083, t1084, t1085);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb102:;
                {
                    void * t1086 = gm_l0;
                    void * t1087 = gm_l1;
                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t1086, t1087);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb103:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 176, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_exprs_r7m130e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_exprs_r7m130e F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 286u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_0202sdh((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
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
                        void * t13 = (void *)((uint8_t *)&F.l2);
                        void * t14 = gm_l0;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t17 = gm_l5;
                        void * t18 = (void *)(((uint8_t *)t15) + (uintptr_t)t17 * 88);
                        gm_t7b7esi3n9r90 t19;
                        gm_f_expr_tp86bm9((void *)&t19, (void *)NULL, cx, NULL, t14, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_0pgacsj(t13, &t19);
                        gm_l6 = t20;
                        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccujoh2vgr9aq, 438, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 438, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l4;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l4 = t25;
        }
    }
gm_lb1:;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
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
inline __attribute__((aligned(4))) void gm_f_float_value_o4b87lc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, double a2) {
    struct gm_fr_float_value_o4b87lc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    double gm_l2; /* x */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t7b7esi3n9r90 gm_l3;
    gm_t53at80pfdus3 gm_l4;
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        void * t10 = gm_l1;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t10));
        gm_f_kind_jcnt4hm((void *)&gm_l4, t9, GM_CONV(gm_csguankko4lmhd, t11));
    }
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)3ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint16_t t14 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = ((t14 == ((uint16_t)16ULL)));
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = gm_l1;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t16));
            double t18 = gm_l2;
            uint16_t t19 = gm_f_f16_bits_2us9irg(t18);
            uint64_t t20 = (uint64_t)t19;
            gm_tfbnciphf3261 t21;
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21 + 0), t17);
            GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t20);
            GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&gm_l3 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&gm_l3 + 16), t21);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)3ULL));
            if (!t23) {
                goto gm_lb3;
            }
            uint16_t t24 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t25 = ((t24 == ((uint16_t)32ULL)));
            if (!t25) {
                goto gm_lb3;
            }
            void * t26 = gm_l1;
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
            double t28 = gm_l2;
            float t29 = (float)((double)t28);
            uint32_t t30;
            memset(&t30, 0, sizeof t30); memcpy(&t30, ((uint8_t *)&t29), sizeof t30 < 4 ? sizeof t30 : 4);
            uint64_t t31 = (uint64_t)t30;
            gm_tfbnciphf3261 t32;
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 0), t27);
            GM_ST(uint64_t, ((uint8_t *)&t32 + 8), t31);
            GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&gm_l3 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&gm_l3 + 16), t32);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
            if (!t34) {
                goto gm_lb4;
            }
            uint16_t t35 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t36 = ((t35 == ((uint16_t)64ULL)));
            if (!t36) {
                goto gm_lb4;
            }
            void * t37 = gm_l1;
            gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t37));
            double t39 = gm_l2;
            uint64_t t40;
            memset(&t40, 0, sizeof t40); memcpy(&t40, ((uint8_t *)&t39), sizeof t40 < 8 ? sizeof t40 : 8);
            gm_tfbnciphf3261 t41;
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41 + 0), t38);
            GM_ST(uint64_t, ((uint8_t *)&t41 + 8), t40);
            GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l3 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&gm_l3 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&gm_l3 + 16), t41);
            goto gm_lb1;
        }
gm_lb4:;
        {
            void * t42 = gm_l0;
            void * t43 = gm_l1;
            gm_f_escape_avsr9t6((void *)&gm_l3, (void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 151, 70);
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 48);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05n2pkm(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_05n2pkm F;
    gm_b16_8 gm_l0; /* inner */
    void * gm_l1; /* el */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
    gm_closure t5 = ((gm_closure){(void *)gm_f_fn_4lhsreh, (void *)&F.h, cx});
    gm_b16_8 t6 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t6;
    {
        void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l9));
        gm_b16_8 t9 = gm_l0;
        gm_tiilmfd16v3fc t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t8);
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
        void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 72));
        gm_t8658kcqovtpe t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t13 + 16), t10);
        gm_b0_1 t14 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t7, &t13, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t15 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24)) != 0);
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24));
            gm_l1 = t16;
            void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
            gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
            void * t19 = gm_l1;
            gm_b0_1 t20 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_24nvriq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_24nvriq F;
    gm_b16_8 gm_l1; /* fail */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l2));
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t4, gm_srccujoh2vgr9aq, 446, 23);
        void * t5 = (void *)(((uint8_t *)t3) + (uintptr_t)((uint64_t)0ULL) * 88);
        gm_f_expr_tp86bm9((void *)&F.l0, (void *)NULL, cx, NULL, t2, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
    gm_closure t7 = ((gm_closure){(void *)gm_f_fn_49psp5f, (void *)&F.h, cx});
    gm_b16_8 t8 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t8;
    {
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
        gm_t7b7esi3n9r90 t10 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
        gm_b16_8 t11 = gm_f_empty_slice_uvn29bg();
        gm_b16_8 t12 = gm_l1;
        gm_t1s3s9ds8udn4 t13;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t13 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 48), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 64), t12);
        void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 72));
        gm_t8658kcqovtpe t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t16 + 16), t13);
        gm_b0_1 t17 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t9, &t16, t15);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_36v8itm(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_36v8itm F;
    uint32_t gm_l1; /* w */
    GM_STACK_CHECK(&F, 1414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tf8kfhcnval78 gm_l2;
    {
        {
            {
                void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
                void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
                gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
                gm_f_word_1sfojus((void *)&gm_l2, (void *)NULL, cx, NULL, t4, t6, ((uint64_t)0ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l2))[48] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l0), ((uint8_t *)&gm_l2), 48);
            }
            {
                void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
                uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
                gm_tlg23kb3oj23s t10;
                gm_f_local_place_qnf3mb2((void *)&t10, t9);
                gm_t7b7esi3n9r90 t11 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
                gm_t6fp3q71vjrmk t12;
                GM_ST(uint64_t, (uint8_t *)&t12 + 56, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t12 + 0), t10);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t12 + 64), t11);
                void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 72));
                gm_t8658kcqovtpe t15;
                memset(&t15, 0, sizeof t15);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)25ULL));
                GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t15 + 16), t12);
                gm_b0_1 t16 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t8, &t15, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t18));
    uint32_t t20 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t20;
    {
        void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        uint32_t t22 = gm_l1;
        gm_tf8kfhcnval78 t23;
        memset(&t23, 0, sizeof t23);
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
        gm_tg85uffmq8b38 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
        GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
        GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t22);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 4), t24);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t25 + 16), t23);
        void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
        gm_t8658kcqovtpe t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t28 + 16), t25);
        gm_b0_1 t29 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t21, &t28, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
        gm_tlg23kb3oj23s t32;
        gm_f_local_place_qnf3mb2((void *)&t32, t31);
        uint32_t t33 = gm_l1;
        gm_t7b7esi3n9r90 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t34 + 16), t33);
        gm_t6fp3q71vjrmk t35;
        GM_ST(uint64_t, (uint8_t *)&t35 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t35 + 0), t32);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t35 + 64), t34);
        void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
        gm_t8658kcqovtpe t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t38 + 16), t35);
        gm_b0_1 t39 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t30, &t38, t37);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sp5ule(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_3sp5ule F;
    uint32_t gm_l0; /* r */
    uint32_t gm_l3; /* w */
    GM_STACK_CHECK(&F, 1938u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbccvikg6b96e gm_l4;
    gm_t9qkh0rsooc8d gm_l5;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    gm_tbccvikg6b96e gm_l8;
    uint8_t t7 = (uint8_t)((((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6))[4] != 0);
    if (t7) {
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6));
        gm_l0 = t8;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
            void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2));
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t11));
            uint8_t t13 = (gm_eq9m6mvtmgde(((uint8_t *)&t10), ((uint8_t *)&t12)));
            if (!t13) {
                goto gm_lb2;
            }
            void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
            uint8_t t17 = (!gm_eq9m6mvtmgde(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) {
                goto gm_lb2;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t19));
                void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
                gm_tlg23kb3oj23s t23 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7));
                gm_f_load_go4fgkp((void *)&F.l1, (void *)NULL, cx, NULL, t18, &t23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                uint32_t t25 = gm_l0;
                gm_tlg23kb3oj23s t26;
                gm_f_local_place_qnf3mb2((void *)&t26, t25);
                gm_t7b7esi3n9r90 t27 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
                gm_t6fp3q71vjrmk t28;
                GM_ST(uint64_t, (uint8_t *)&t28 + 56, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t28 + 0), t26);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t28 + 64), t27);
                void * t29 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
                gm_t8658kcqovtpe t31;
                memset(&t31, 0, sizeof t31);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)25ULL));
                GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t31 + 16), t28);
                gm_b0_1 t32 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t24, &t31, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        memset(&F.l2, 0, sizeof F.l2);
        GM_ST(uint8_t, ((uint8_t *)&F.l2 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l2 + 16), t33);
    }
    {
        void * t34 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t37 = (t36 > ((uint64_t)0ULL));
    if (t37) {
        void * t38 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l9));
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        gm_tlcm4lvu3g1a8 t41;
        GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t39);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41 + 8), t40);
        gm_tfqmtuprvunod t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t42 + 16), t41);
        gm_tlg23kb3oj23s t43;
        gm_f_project_6hpd48e((void *)&t43, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7)), &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 72));
        gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t38, &t43, t44, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t47));
    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
    uint8_t t50 = (!gm_eq9m6mvtmgde(((uint8_t *)&t48), ((uint8_t *)&t49)));
    if (t50) {
        void * t51 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        void * t52 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t52));
        uint32_t t54 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t51, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t54;
        {
            void * t55 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
            uint32_t t56 = gm_l3;
            void * t57 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
            void * t58 = (void *)(((uint8_t *)t57) + 8);
            void * t59 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t59));
            gm_f_kind_jcnt4hm((void *)&gm_l7, t58, GM_CONV(gm_csguankko4lmhd, t60));
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)13ULL));
                    if (!t62) {
                        goto gm_lb4;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1184, 33);
            }
gm_lb3:;
            uint8_t t63 = gm_l6;
            if (t63) {
                gm_t9qkh0rsooc8d t64;
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
                gm_l5 = t64;
            } else {
                gm_t9qkh0rsooc8d t65;
                GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)2ULL));
                gm_l5 = t65;
            }
            gm_t9qkh0rsooc8d t66 = gm_l5;
            gm_t7b7esi3n9r90 t67 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
            gm_tf8kfhcnval78 t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t68 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t68), t67);
            ((uint8_t *)&t68)[48] = 1;
            gm_tg85uffmq8b38 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
            GM_ST(uint32_t, ((uint8_t *)&t69 + 0), t56);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t69 + 4), t66);
            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t69 + 16), t68);
            void * t70 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
            gm_t8658kcqovtpe t72;
            memset(&t72, 0, sizeof t72);
            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t72 + 16), t69);
            gm_b0_1 t73 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t55, &t72, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t74 = gm_l3;
            memset(&F.l2, 0, sizeof F.l2);
            GM_ST(uint8_t, ((uint8_t *)&F.l2 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&F.l2 + 16), t74);
        }
    } else {
        void * t75 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        gm_f_layout_a5ovnrm((void *)&gm_l8, (void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
        if (t78) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    {
        void * t79 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        uint32_t t80 = gm_l0;
        gm_tlg23kb3oj23s t81;
        gm_f_local_place_qnf3mb2((void *)&t81, t80);
        gm_t7b7esi3n9r90 t82 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
        gm_t6fp3q71vjrmk t83;
        GM_ST(uint64_t, (uint8_t *)&t83 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t83 + 0), t81);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t83 + 64), t82);
        void * t84 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
        gm_t8658kcqovtpe t86;
        memset(&t86, 0, sizeof t86);
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t86 + 16), t83);
        gm_b0_1 t87 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t79, &t86, t85);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_49psp5f(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_49psp5f F;
    GM_STACK_CHECK(&F, 389u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        memset(&F.l0, 0, sizeof F.l0);
    }
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2) + 8));
    uint8_t t3 = (t2 > ((uint64_t)1ULL));
    if (t3) {
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l0));
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t6, gm_srccujoh2vgr9aq, 449, 46);
        void * t7 = (void *)(((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 88);
        gm_t7b7esi3n9r90 t8;
        gm_f_expr_tp86bm9((void *)&t8, (void *)NULL, cx, NULL, t4, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l0 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l0 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l0 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&F.l0 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0), t8);
        ((uint8_t *)&F.l0)[48] = 1;
    }
    {
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l0));
        gm_tf8kfhcnval78 t10 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l0));
        void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l1));
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 72));
        gm_t8658kcqovtpe t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)35ULL));
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t13 + 16), t10);
        gm_b0_1 t14 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t9, &t13, t12);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4lhsreh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_4lhsreh F;
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l9));
    gm_b0_1 t5 = gm_f_conds_sqc7llk((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    gm_t9805qtonbsag t7 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l3));
    void * t8 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
    gm_b0_1 t9 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    void * t11 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
    uint8_t t12 = gm_f_diverges_p3okmol(t10, t11);
    uint8_t t13 = (uint8_t)!t12;
    if (t13) {
        void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l8));
        void * t16 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
        gm_t8658kcqovtpe t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)30ULL));
        GM_ST(uint32_t, ((uint8_t *)&t18 + 16), t15);
        gm_b0_1 t19 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t14, &t18, t17);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4oh4m1v(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_67nerak(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_67nerak F;
    gm_b16_8 gm_l0; /* inner */
    void * gm_l1; /* st */
    GM_STACK_CHECK(&F, 453u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 8));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l4));
    gm_b0_1 t7 = gm_f_conds_sqc7llk((void *)NULL, cx, NULL, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_roenhir, (void *)&F.h, cx});
    gm_b16_8 t10 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t10;
    {
        void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
        uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l6));
        gm_b16_8 t13 = gm_l0;
        gm_tiilmfd16v3fc t14;
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
        void * t15 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 24));
        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 24));
        gm_t8658kcqovtpe t17;
        memset(&t17, 0, sizeof t17);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t17 + 16), t14);
        gm_b0_1 t18 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t17, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t19 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 32)) != 0);
            if (!t19) {
                goto gm_lb2;
            }
            void * t20 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 32));
            gm_l1 = t20;
            void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
            void * t22 = gm_l1;
            gm_t7b7esi3n9r90 t23;
            gm_f_expr_tp86bm9((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74evcu5(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a8eu0io(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cccid3f(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cccid3f F;
    gm_b16_8 gm_l0; /* looped */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l0));
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_67nerak, (void *)&F.h, cx});
    gm_b16_8 t4 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t4;
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l0));
        uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l5));
        gm_b16_8 t7 = gm_l0;
        gm_tiilmfd16v3fc t8;
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t6);
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
        gm_t8658kcqovtpe t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)28ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t11 + 16), t8);
        gm_b0_1 t12 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t11, t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cquk74j(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cquk74j F;
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t gm_l0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l5));
        gm_tlg23kb3oj23s t4;
        gm_f_local_place_qnf3mb2((void *)&t4, t3);
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l1));
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l2));
        if (t7) {
            gm_l0 = ((uint64_t)0ULL);
        } else {
            gm_l0 = ((uint64_t)1ULL);
        }
        uint64_t t8 = gm_l0;
        gm_tfbnciphf3261 t9;
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t9 + 0), t6);
        GM_ST(uint64_t, ((uint8_t *)&t9 + 8), t8);
        gm_t7b7esi3n9r90 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t10 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t10 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t10 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t10 + 16), t9);
        gm_t6fp3q71vjrmk t11;
        GM_ST(uint64_t, (uint8_t *)&t11 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t11 + 0), t4);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 64), t10);
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
        gm_t8658kcqovtpe t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t14 + 16), t11);
        gm_b0_1 t15 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t2, &t14, t13);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dduu5o8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_dduu5o8 F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_b16_8 gm_l4; /* insts */
    GM_STACK_CHECK(&F, 701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l42));
    gm_l0 = t7;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l1;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l1;
                {
                    {
                        void * t15 = GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                        F.l2 = t15;
                        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                        uint32_t t17 = gm_f_label_3373lil(t16);
                        F.l3 = t17;
                        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                        gm_closure t19 = ((gm_closure){(void *)gm_f_fn_v8tau4j, (void *)&F.h, cx});
                        gm_b16_8 t20 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t18, t19);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l4 = t20;
                        {
                            void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                            uint32_t t22 = F.l3;
                            gm_b16_8 t23 = gm_l4;
                            gm_tiilmfd16v3fc t24;
                            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t22);
                            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
                            void * t25 = F.l2;
                            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 120));
                            gm_t8658kcqovtpe t27;
                            memset(&t27, 0, sizeof t27);
                            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)27ULL));
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t27 + 16), t24);
                            gm_b0_1 t28 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t21, &t27, t26);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1097, 17);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l1;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l1 = t32;
        }
    }
    goto gm_ls33;
gm_ll8:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls33:;
gm_lb1:;
    void * t34 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
    uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l3));
    void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 120));
    gm_t8658kcqovtpe t38;
    memset(&t38, 0, sizeof t38);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)30ULL));
    GM_ST(uint32_t, ((uint8_t *)&t38 + 16), t35);
    gm_b0_1 t39 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t34, &t38, t37);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_drdvs2i(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_drdvs2i F;
    GM_STACK_CHECK(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l4));
        gm_f_expr_tp86bm9((void *)&F.l0, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l5));
        gm_tlg23kb3oj23s t6;
        gm_f_local_place_qnf3mb2((void *)&t6, t5);
        gm_t7b7esi3n9r90 t7 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
        gm_t6fp3q71vjrmk t8;
        GM_ST(uint64_t, (uint8_t *)&t8 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t8 + 0), t6);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t8 + 64), t7);
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l4));
        gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
        gm_t8658kcqovtpe t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t11 + 16), t8);
        gm_b0_1 t12 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t4, &t11, t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_etfgial(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_etfgial F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_b16_8 gm_l4; /* insts */
    GM_STACK_CHECK(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l3));
    gm_l0 = t7;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l1;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l1;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l2), (((uint8_t *)t12) + (uintptr_t)t14 * 24), 24);
                        }
                        void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                        uint32_t t16 = gm_f_label_3373lil(t15);
                        F.l3 = t16;
                        void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                        gm_closure t18 = ((gm_closure){(void *)gm_f_fn_pmnv16v, (void *)&F.h, cx});
                        gm_b16_8 t19 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l4 = t19;
                        {
                            void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                            uint32_t t21 = F.l3;
                            gm_b16_8 t22 = gm_l4;
                            gm_tiilmfd16v3fc t23;
                            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t21);
                            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
                            void * t24 = GM_LD(void *, ((uint8_t *)&F.l2));
                            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 120));
                            gm_t8658kcqovtpe t26;
                            memset(&t26, 0, sizeof t26);
                            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)27ULL));
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t26 + 16), t23);
                            gm_b0_1 t27 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t26, t25);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 996, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l1;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l1 = t31;
        }
    }
    goto gm_ls32;
gm_ll8:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls32:;
gm_lb1:;
    void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
    void * t34 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
    gm_t8658kcqovtpe t36;
    memset(&t36, 0, sizeof t36);
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)42ULL));
    gm_b0_1 t37 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t33, &t36, t35);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0ofpte(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_k0ofpte F;
    uint32_t gm_l1; /* d */
    gm_t88rcucg1b7uk gm_l2; /* err */
    uint32_t gm_l4; /* variant */
    gm_t88rcucg1b7uk gm_l5; /* ferr */
    uint32_t gm_l6; /* lifted */
    uint32_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 2111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tf8kfhcnval78 gm_l8;
    gm_t53at80pfdus3 gm_l9;
    gm_tbccvikg6b96e gm_l10;
    gm_t53at80pfdus3 gm_l11;
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l6));
    uint8_t t11 = (uint8_t)!t10;
    if (t11) {
        {
            {
                {
                    void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
                    gm_f_word_1sfojus((void *)&gm_l8, (void *)NULL, cx, NULL, t12, t13, ((uint64_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l8))[48] != 0);
                if (!t14) {
                    goto gm_lb2;
                }
                {
                    memmove(((uint8_t *)&F.l0), ((uint8_t *)&gm_l8), 48);
                }
                {
                    gm_closure t15 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
                    gm_closure t16 = t15;
                    gm_node *ccx17 = t16.cx;
                    void * t18 = t16.code;
                    gm_b0_1 t19;
                    if (GM_PTR_TAGGED(t18)) {
                        ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t18))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l0)));
                    } else {
                        t19 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t18)(t16.env, ccx17, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l0)));
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
        uint32_t t22 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t22;
        {
            void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
            uint32_t t24 = gm_l1;
            gm_tf8kfhcnval78 t25;
            memset(&t25, 0, sizeof t25);
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            gm_tg85uffmq8b38 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 8, 0);
            GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t24);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 4), t26);
            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t27 + 16), t25);
            void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
            gm_t8658kcqovtpe t30;
            memset(&t30, 0, sizeof t30);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t30 + 16), t27);
            gm_b0_1 t31 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t23, &t30, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t32 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
            uint32_t t33 = gm_l1;
            gm_t7b7esi3n9r90 t34;
            memset(&t34, 0, sizeof t34);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t34 + 16), t33);
            gm_closure t35 = t32;
            gm_node *ccx36 = t35.cx;
            void * t37 = t35.code;
            gm_b0_1 t38;
            if (GM_PTR_TAGGED(t37)) {
                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t37))(t34);
            } else {
                t38 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t37)(t35.env, ccx36, NULL, &t34);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            {
                void * t39 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
                void * t40 = (void *)(((uint8_t *)t39) + 8);
                void * t41 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2));
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t41));
                gm_f_kind_jcnt4hm((void *)&gm_l9, t40, GM_CONV(gm_csguankko4lmhd, t42));
            }
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)14ULL));
            if (!t44) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t45;
            {
                gm_t88rcucg1b7uk t46 = gm_l2;
                memset(&F.l3, 0, sizeof F.l3);
                GM_ST(uint8_t, ((uint8_t *)&F.l3 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l3 + 16), t46);
            }
            {
                void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
                gm_t88rcucg1b7uk t48 = gm_l2;
                gm_f_layout_a5ovnrm((void *)&gm_l10, (void *)NULL, cx, NULL, t47, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
            uint8_t t50 = (t49 > ((uint64_t)0ULL));
            if (t50) {
                void * t51 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l9));
                gm_t88rcucg1b7uk t53 = gm_l2;
                gm_tlcm4lvu3g1a8 t54;
                GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t52);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t54 + 8), t53);
                gm_tfqmtuprvunod t55;
                memset(&t55, 0, sizeof t55);
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t55 + 16), t54);
                gm_tlg23kb3oj23s t56;
                gm_f_project_6hpd48e((void *)&t56, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l7)), &t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t57 = gm_l2;
                void * t58 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
                gm_f_load_go4fgkp((void *)&F.l3, (void *)NULL, cx, NULL, t51, &t56, t57, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t60 = (uint8_t)(((((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 8))[4] != 0);
                    if (!t60) {
                        goto gm_lb6;
                    }
                    uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 8));
                    gm_l4 = t61;
                    {
                        {
                            {
                                void * t62 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
                                void * t63 = (void *)(((uint8_t *)t62) + 8);
                                gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
                                gm_f_kind_jcnt4hm((void *)&gm_l11, t63, GM_CONV(gm_csguankko4lmhd, t64));
                            }
                            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)14ULL));
                            if (!t66) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 8));
                            gm_l5 = t67;
                            void * t68 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                            gm_t88rcucg1b7uk t69 = gm_l5;
                            uint32_t t70 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t68, t69);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l6 = t70;
                            {
                                void * t71 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                                uint32_t t72 = gm_l6;
                                uint32_t t73 = gm_l4;
                                gm_t7b7esi3n9r90 t74 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
                                gm_tf8kfhcnval78 t75;
                                GM_ST(uint8_t, (uint8_t *)&t75 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t75 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t75 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&t75 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t75), t74);
                                ((uint8_t *)&t75)[48] = 1;
                                gm_tvv8lnu6a3ppj t76;
                                GM_ST(uint64_t, (uint8_t *)&t76 + 8, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t76 + 0), t72);
                                GM_ST(uint32_t, ((uint8_t *)&t76 + 4), t73);
                                GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t76 + 16), t75);
                                void * t77 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
                                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                                gm_t8658kcqovtpe t79;
                                memset(&t79, 0, sizeof t79);
                                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)14ULL));
                                GM_ST(gm_tvv8lnu6a3ppj, ((uint8_t *)&t79 + 16), t76);
                                gm_b0_1 t80 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t71, &t79, t78);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                uint32_t t81 = gm_l6;
                                memset(&F.l3, 0, sizeof F.l3);
                                GM_ST(uint8_t, ((uint8_t *)&F.l3 + 0), ((uint8_t)0ULL));
                                GM_ST(uint32_t, ((uint8_t *)&F.l3 + 16), t81);
                            }
                            goto gm_lb7;
                        }
gm_lb8:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            void * t82 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
            uint32_t t84 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t84;
            {
                void * t85 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                uint32_t t86 = gm_l7;
                gm_t7b7esi3n9r90 t87 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
                gm_tf8kfhcnval78 t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&t88 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t88), t87);
                ((uint8_t *)&t88)[48] = 1;
                gm_t9qkh0rsooc8d t89;
                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)3ULL));
                gm_tg85uffmq8b38 t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 6, 0);
                GM_ST(uint64_t, (uint8_t *)&t90 + 8, 0);
                GM_ST(uint32_t, ((uint8_t *)&t90 + 0), t86);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t90 + 4), t89);
                GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t90 + 16), t88);
                void * t91 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 72));
                gm_t8658kcqovtpe t93;
                memset(&t93, 0, sizeof t93);
                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)15ULL));
                GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t93 + 16), t90);
                gm_b0_1 t94 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t85, &t93, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_closure t95 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
                uint32_t t96 = gm_l7;
                gm_t7b7esi3n9r90 t97;
                memset(&t97, 0, sizeof t97);
                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t97 + 16), t96);
                gm_closure t98 = t95;
                gm_node *ccx99 = t98.cx;
                void * t100 = t98.code;
                gm_b0_1 t101;
                if (GM_PTR_TAGGED(t100)) {
                    ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t100))(t97);
                } else {
                    t101 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t100)(t98.env, ccx99, NULL, &t97);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb3;
        }
gm_lb4:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
gm_lb3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k3pfo3n(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_k3pfo3n F;
    void * gm_l0; /* tail */
    uint32_t gm_l2; /* r */
    GM_STACK_CHECK(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
    void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l1));
    gm_b0_1 t8 = gm_f_stmts_ctl06qt((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l1));
            uint8_t t10 = (uint8_t)(GM_LD(void *, (((uint8_t *)t9) + 16)) != 0);
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, (((uint8_t *)t9) + 16));
            gm_l0 = t11;
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
            void * t13 = gm_l0;
            gm_f_expr_tp86bm9((void *)&F.l1, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t14 = (uint8_t)((((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l9))[4] != 0);
                    if (!t14) {
                        goto gm_lb4;
                    }
                    uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l9));
                    gm_l2 = t15;
                    {
                        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
                        uint32_t t17 = gm_l2;
                        gm_tlg23kb3oj23s t18;
                        gm_f_local_place_qnf3mb2((void *)&t18, t17);
                        gm_t7b7esi3n9r90 t19 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
                        gm_t6fp3q71vjrmk t20;
                        GM_ST(uint64_t, (uint8_t *)&t20 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t20 + 0), t18);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t20 + 64), t19);
                        void * t21 = gm_l0;
                        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
                        gm_t8658kcqovtpe t23;
                        memset(&t23, 0, sizeof t23);
                        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t23 + 16), t20);
                        gm_b0_1 t24 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t16, &t23, t22);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_m7prcu1(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_m7prcu1 F;
    uint32_t gm_l0; /* p */
    uint32_t gm_l3; /* d */
    uint32_t gm_l4; /* w */
    GM_STACK_CHECK(&F, 2001u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbccvikg6b96e gm_l5;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l12))[4] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l12));
            gm_l0 = t5;
            {
                uint32_t t6 = gm_l0;
                uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l9));
                gm_t7b7esi3n9r90 t8 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l19));
                gm_t592joge1j4uv t9;
                GM_ST(uint64_t, (uint8_t *)&t9 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t9 + 0), t8);
                GM_ST(uint64_t, ((uint8_t *)&t9 + 48), t7);
                gm_tu78tssgij60g t10;
                memset(&t10, 0, sizeof t10);
                GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)4ULL));
                GM_ST(uint32_t, ((uint8_t *)&t10 + 8), t6);
                gm_tfqmtuprvunod t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t11 + 16), t9);
                gm_b16_8 t12 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&F.l1 + 0), t10);
                GM_ST(gm_b16_8, ((uint8_t *)&F.l1 + 40), t12);
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l9));
            gm_t7b7esi3n9r90 t14 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l19));
            gm_t592joge1j4uv t15;
            GM_ST(uint64_t, (uint8_t *)&t15 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t15 + 0), t14);
            GM_ST(uint64_t, ((uint8_t *)&t15 + 48), t13);
            gm_tlg23kb3oj23s t16 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l11));
            gm_tfqmtuprvunod t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t17 + 16), t15);
            gm_f_project_6hpd48e((void *)&F.l1, (void *)NULL, cx, NULL, &t16, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
        memset(&F.l2, 0, sizeof F.l2);
        GM_ST(uint8_t, ((uint8_t *)&F.l2 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l2 + 16), t18);
    }
    gm_t8588ma2cr9d7 t19 = GM_LD(gm_t8588ma2cr9d7, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l2));
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
    uint8_t t21 = (uint8_t)((t20 == ((uint8_t)37ULL)));
    gm_t8588ma2cr9d7 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)37ULL));
    if (t21) {
        void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l6));
        uint32_t t25 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t25;
        {
            void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
            uint32_t t27 = gm_l3;
            gm_tlg23kb3oj23s t28 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
            gm_tlpcrb3vu6o2d t29;
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t29 + 0), t27);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t29 + 8), t28);
            void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            gm_t8658kcqovtpe t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t32 + 16), t29);
            gm_b0_1 t33 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t26, &t32, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t34 = gm_l3;
            memset(&F.l2, 0, sizeof F.l2);
            GM_ST(uint8_t, ((uint8_t *)&F.l2 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&F.l2 + 16), t34);
        }
    } else {
        void * t35 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
        gm_f_layout_a5ovnrm((void *)&gm_l5, (void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        uint8_t t38 = (t37 > ((uint64_t)0ULL));
        if (t38) {
            void * t39 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
            void * t41 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
            gm_tlg23kb3oj23s t43 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
            gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t39, &t43, t40, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    void * t44 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t45));
    uint32_t t47 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t44, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t47;
    {
        void * t48 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        uint32_t t49 = gm_l4;
        gm_t7b7esi3n9r90 t50 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
        gm_tf8kfhcnval78 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t51), t50);
        ((uint8_t *)&t51)[48] = 1;
        gm_t9qkh0rsooc8d t52;
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
        gm_tg85uffmq8b38 t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 6, 0);
        GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
        GM_ST(uint32_t, ((uint8_t *)&t53 + 0), t49);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t53 + 4), t52);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t53 + 16), t51);
        void * t54 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
        gm_t8658kcqovtpe t56;
        memset(&t56, 0, sizeof t56);
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t56 + 16), t53);
        gm_b0_1 t57 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t48, &t56, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t58 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        uint32_t t59 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
        gm_tlg23kb3oj23s t60;
        gm_f_local_place_qnf3mb2((void *)&t60, t59);
        uint32_t t61 = gm_l4;
        gm_t7b7esi3n9r90 t62;
        memset(&t62, 0, sizeof t62);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t62 + 16), t61);
        gm_t6fp3q71vjrmk t63;
        GM_ST(uint64_t, (uint8_t *)&t63 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t63 + 0), t60);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t63 + 64), t62);
        void * t64 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
        gm_t8658kcqovtpe t66;
        memset(&t66, 0, sizeof t66);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t66 + 16), t63);
        gm_b0_1 t67 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t58, &t66, t65);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mh72upo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_mh72upo F;
    void * gm_l0; /* n */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)16ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t9;
            uint32_t t10 = GM_LD(uint32_t, (((((uint8_t *)t6) + 8) + 8) + 4));
            gm_l2 = t10;
            {
                {
                    uint32_t t11 = gm_l1;
                    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l2));
                    uint8_t t13 = ((t11 == t12));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    uint32_t t14 = gm_l2;
                    uint64_t t15 = (uint64_t)t14;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4) + 8));
                    uint8_t t17 = (t15 < t16);
                    if (!t17) {
                        goto gm_lb4;
                    }
                    void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4));
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4) + 8));
                    uint32_t t20 = gm_l2;
                    uint64_t t21 = (uint64_t)t20;
                    if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_srccujoh2vgr9aq, 678, 68);
                    GM_ST(uint8_t, ((((uint8_t *)t18) + (uintptr_t)t21 * 32) + 26), ((uint8_t)1));
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
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo8l3p0(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mo8l3p0 F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_t4lnf71mruqpt gm_l2; /* h */
    void * gm_l3; /* x */
    uint32_t gm_l4; /* id */
    uint32_t gm_l6; /* id */
    uint32_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* decls */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 1545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t53at80pfdus3 gm_l11;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l2) + 8));
    gm_l0 = t14;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l1;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t20 = gm_l1;
                {
                    {
                        memmove(((uint8_t *)&gm_l2), (((uint8_t *)t18) + (uintptr_t)t20 * 32), 32);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                void * t23 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
                                gm_l3 = t23;
                                {
                                    {
                                        void * t24 = gm_l3;
                                        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
                                        uint8_t t26 = (uint8_t)(t25 == ((uint8_t)28ULL));
                                        if (!t26) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t24) + 8) + 8));
                                        gm_l4 = t27;
                                        {
                                            void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                            uint32_t t29 = gm_l4;
                                            gm_tkgci6o4gktbf t30;
                                            memset(&t30, 0, sizeof t30);
                                            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
                                            GM_ST(uint32_t, ((uint8_t *)&t30 + 16), t29);
                                            void * t31 = gm_l3;
                                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                                            gm_t8658kcqovtpe t33;
                                            memset(&t33, 0, sizeof t33);
                                            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)38ULL));
                                            GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t33 + 16), t30);
                                            gm_b0_1 t34 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t33, t32);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        goto gm_lb3;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                {
                                    void * t35 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                    void * t36 = gm_l3;
                                    gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t35, t36);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                gm_l6 = ((uint32_t)0ULL);
                                {
                                    {
                                        void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l3));
                                        void * t38 = (void *)(((uint8_t *)t37) + 8);
                                        void * t39 = gm_l3;
                                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t39));
                                        gm_f_kind_jcnt4hm((void *)&gm_l11, t38, GM_CONV(gm_csguankko4lmhd, t40));
                                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                        uint8_t t42 = (uint8_t)(t41 == ((uint8_t)25ULL));
                                        if (!t42) {
                                            goto gm_lb11;
                                        }
                                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                                        gm_l7 = t43;
                                        uint32_t t44 = gm_l7;
                                        gm_l6 = t44;
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                {
                                    void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                    uint32_t t46 = gm_l6;
                                    gm_t7b7esi3n9r90 t47 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                                    gm_tf4lk4crue8vt t48;
                                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t46);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t48 + 16), t47);
                                    gm_tkgci6o4gktbf t49;
                                    GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t49 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
                                    GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t49 + 16), t48);
                                    void * t50 = gm_l3;
                                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 72));
                                    gm_t8658kcqovtpe t52;
                                    memset(&t52, 0, sizeof t52);
                                    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)38ULL));
                                    GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t52 + 16), t49);
                                    gm_b0_1 t53 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t45, &t52, t51);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                                if (!t55) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
                                gm_l8 = t56;
                                void * t57 = GM_LD(void *, ((((uint8_t *)&gm_l2) + 8) + 16));
                                gm_l9 = t57;
                                {
                                    void * t58 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                    void * t59 = gm_l9;
                                    gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t58, t59);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    void * t60 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                    gm_b16_8 t61 = gm_l8;
                                    gm_t7b7esi3n9r90 t62 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                                    gm_tmvmieab1acso t63;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t61);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t63 + 16), t62);
                                    gm_tkgci6o4gktbf t64;
                                    GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t64 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_tmvmieab1acso, ((uint8_t *)&t64 + 16), t63);
                                    void * t65 = gm_l9;
                                    gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                                    gm_t8658kcqovtpe t67;
                                    memset(&t67, 0, sizeof t67);
                                    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)38ULL));
                                    GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t67 + 16), t64);
                                    gm_b0_1 t68 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t60, &t67, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1265, 13);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1264, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t69 = gm_l1;
            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
            uint64_t t72 = r70;
            gm_l1 = t72;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pmnv16v(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_pmnv16v F;
    void * gm_l0; /* g */
    GM_STACK_CHECK(&F, 687u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l3));
    gm_b0_1 t7 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t4, t5, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l5)), t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t8 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2) + 8)) != 0);
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2) + 8));
            gm_l0 = t9;
            {
                void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
                void * t11 = gm_l0;
                gm_f_expr_tp86bm9((void *)&F.l1, (void *)NULL, cx, NULL, t10, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
                uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l3));
                gm_t7b7esi3n9r90 t14 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
                gm_tejc6vv5sqno9 t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 50, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t15 + 0), t14);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 48), ((uint8_t)1));
                GM_ST(uint32_t, ((uint8_t *)&t15 + 52), t13);
                void * t16 = gm_l0;
                gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
                gm_t8658kcqovtpe t18;
                memset(&t18, 0, sizeof t18);
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)31ULL));
                GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t18 + 16), t15);
                gm_b0_1 t19 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t12, &t18, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    gm_t9805qtonbsag t21 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l4));
    void * t22 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t23 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t20, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    void * t25 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2) + 16));
    uint8_t t26 = gm_f_diverges_p3okmol(t24, t25);
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
        uint32_t t29 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l6));
        void * t30 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_etfgial *)((&F.h)->up))->l2) + 16));
        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
        gm_t8658kcqovtpe t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)30ULL));
        GM_ST(uint32_t, ((uint8_t *)&t32 + 16), t29);
        gm_b0_1 t33 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t32, t31);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rihlhf9(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rihlhf9 F;
    uint32_t gm_l0; /* c */
    gm_t88rcucg1b7uk gm_l1; /* err */
    GM_STACK_CHECK(&F, 832u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t53at80pfdus3 gm_l3;
    gm_tbccvikg6b96e gm_l4;
    {
        {
            uint8_t t6 = (uint8_t)(((((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 8))[4] != 0);
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 8));
            gm_l0 = t7;
            {
                {
                    {
                        void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
                        void * t9 = (void *)(((uint8_t *)t8) + 8);
                        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2));
                        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t10));
                        gm_f_kind_jcnt4hm((void *)&gm_l3, t9, GM_CONV(gm_csguankko4lmhd, t11));
                    }
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)14ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
                    gm_l1 = t14;
                    {
                        void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
                        gm_t88rcucg1b7uk t16 = gm_l1;
                        gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t15, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
                    uint8_t t18 = (t17 > ((uint64_t)0ULL));
                    if (t18) {
                        {
                            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                            uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l9));
                            gm_t88rcucg1b7uk t21 = gm_l1;
                            gm_tlcm4lvu3g1a8 t22;
                            GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 8), t21);
                            gm_tfqmtuprvunod t23;
                            memset(&t23, 0, sizeof t23);
                            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t23 + 16), t22);
                            gm_tlg23kb3oj23s t24;
                            gm_f_project_6hpd48e((void *)&t24, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7)), &t23);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t88rcucg1b7uk t25 = gm_l1;
                            void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                            gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
                            gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t19, &t24, t25, t27);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                            uint32_t t29 = gm_l0;
                            gm_tlg23kb3oj23s t30;
                            gm_f_local_place_qnf3mb2((void *)&t30, t29);
                            gm_t7b7esi3n9r90 t31 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
                            gm_t6fp3q71vjrmk t32;
                            GM_ST(uint64_t, (uint8_t *)&t32 + 56, 0);
                            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t32 + 0), t30);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t32 + 64), t31);
                            void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                            gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                            gm_t8658kcqovtpe t35;
                            memset(&t35, 0, sizeof t35);
                            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)25ULL));
                            GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t35 + 16), t32);
                            gm_b0_1 t36 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t35, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t38 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6));
    void * t39 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t40 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t37, t38, t39);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v8tau4j(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_v8tau4j F;
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l0));
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l2));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l3));
        gm_b0_1 t5 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t2, t3, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l2)), t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l0));
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l43));
        void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l2));
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 120));
        gm_t8658kcqovtpe t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)30ULL));
        GM_ST(uint32_t, ((uint8_t *)&t10 + 16), t7);
        gm_b0_1 t11 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t6, &t10, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vkfbupa(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_vkfbupa F;
    void * gm_l0; /* n */
    void * gm_l1; /* x */
    void * gm_l2; /* x */
    gm_tao63pk3r18v5 gm_l3; /* sl */
    GM_STACK_CHECK(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t12 = gm_l0;
    {
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)24ULL));
                    if (!t14) {
                        goto gm_lb4;
                    }
                    void * t15 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
                    gm_l1 = t15;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)25ULL));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    void * t18 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
                    gm_l1 = t18;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
            void * t20 = gm_l1;
            gm_f_mark_root_b973vvi(t19, t20);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)21ULL));
            if (!t22) {
                goto gm_lb6;
            }
            {
                memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t12) + 8) + 8), 32);
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
                void * t24 = GM_LD(void *, ((uint8_t *)t23));
                void * t25 = (void *)(((uint8_t *)t24) + 8);
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l3));
                gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
                gm_f_kind_jcnt4hm((void *)&gm_l5, t25, GM_CONV(gm_csguankko4lmhd, t27));
            }
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)11ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 137, 30);
            }
gm_lb7:;
            uint8_t t30 = gm_l4;
            if (t30) {
                void * t31 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l3));
                gm_f_mark_root_b973vvi(t31, t32);
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 135, 9);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_get_d0bmskj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3) {
    struct gm_fr_get_d0bmskj F;
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l5; /* usize_ty */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    void * gm_l10; /* base */
    uint64_t gm_l14; /* n */
    uint8_t gm_l15; /* is_const */
    gm_t88rcucg1b7uk gm_l16; /* el */
    gm_t88rcucg1b7uk gm_l18; /* pt */
    uint32_t gm_l20; /* d */
    uint32_t gm_l22; /* ok */
    gm_b16_8 gm_l23; /* found */
    gm_b16_8 gm_l24; /* missing */
    GM_STACK_CHECK(&F, 3358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_tbccvikg6b96e gm_l27;
    gm_t53at80pfdus3 gm_l28;
    gm_tlg23kb3oj23s gm_l29;
    gm_tfonjjajjj3jq gm_l30;
    gm_tfonjjajjj3jq gm_l31;
    void * t14 = F.l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    F.l4 = t15;
    void * t16 = F.l4;
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_t88rcucg1b7uk t18 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t18;
    {
        {
            {
                void * t19 = F.l4;
                void * t20 = (void *)(((uint8_t *)t19) + 8);
                void * t21 = F.l1;
                gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
                gm_f_kind_jcnt4hm((void *)&gm_l25, t20, GM_CONV(gm_csguankko4lmhd, t22));
            }
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)13ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 8));
            F.l6 = t25;
            gm_t88rcucg1b7uk t26 = F.l6;
            F.l7 = t26;
            gm_t8588ma2cr9d7 t27 = F.l2;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            uint8_t t29 = (uint8_t)((t28 == ((uint8_t)37ULL)));
            gm_t8588ma2cr9d7 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)37ULL));
            if (t29) {
                {
                    {
                        void * t31 = F.l4;
                        void * t32 = (void *)(((uint8_t *)t31) + 8);
                        gm_t88rcucg1b7uk t33 = F.l6;
                        gm_f_kind_jcnt4hm((void *)&gm_l26, t32, GM_CONV(gm_csguankko4lmhd, t33));
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        uint8_t t35 = (uint8_t)(t34 == ((uint8_t)8ULL));
                        if (!t35) {
                            goto gm_lb4;
                        }
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
                        gm_l8 = t36;
                        gm_t88rcucg1b7uk t37 = gm_l8;
                        F.l7 = t37;
                        goto gm_lb3;
                    }
gm_lb4:;
                }
gm_lb3:;
            }
            {
                void * t38 = F.l4;
                gm_t88rcucg1b7uk t39 = F.l7;
                gm_f_layout_a5ovnrm((void *)&gm_l27, (void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
            F.l9 = t40;
            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t42, gm_srccujoh2vgr9aq, 468, 14);
            void * t43 = (void *)(((uint8_t *)t41) + (uintptr_t)((uint64_t)0ULL) * 88);
            gm_l10 = t43;
            {
                gm_tlg23kb3oj23s t44;
                gm_f_local_place_qnf3mb2((void *)&t44, ((uint32_t)0ULL));
                F.l11 = t44;
            }
            gm_t9805qtonbsag t45;
            memset(&t45, 0, sizeof t45);
            F.l12 = t45;
            {
                gm_t88rcucg1b7uk t46 = gm_l5;
                gm_tfbnciphf3261 t47;
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 0), t46);
                GM_ST(uint64_t, ((uint8_t *)&t47 + 8), ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&F.l13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&F.l13 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&F.l13 + 16), t47);
            }
            {
                void * t48 = F.l4;
                void * t49 = (void *)(((uint8_t *)t48) + 8);
                void * t50 = gm_l10;
                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
                gm_f_kind_jcnt4hm((void *)&gm_l28, t49, GM_CONV(gm_csguankko4lmhd, t51));
            }
            {
                {
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)11ULL));
                    if (!t53) {
                        goto gm_lb6;
                    }
                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                    gm_l14 = t54;
                    {
                        void * t55 = F.l0;
                        void * t56 = gm_l10;
                        gm_f_operand_place_cdle4ls((void *)&gm_l30, (void *)NULL, cx, NULL, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l30))[56] != 0);
                    if (t57) {
                        memmove(((uint8_t *)&gm_l29), ((uint8_t *)&gm_l30), 56);
                    } else {
                        void * t58 = F.l0;
                        void * t59 = F.l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l29), 56);
                    }
                    gm_t8588ma2cr9d7 t60 = F.l2;
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                    uint8_t t62 = (uint8_t)((t61 == ((uint8_t)37ULL)));
                    gm_t8588ma2cr9d7 t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)37ULL));
                    if (t62) {
                        void * t64 = F.l0;
                        void * t65 = (void *)((uint8_t *)&F.l11);
                        gm_f_mark_addressed_t8qojin(t64, t65);
                    }
                    {
                        gm_t88rcucg1b7uk t66 = gm_l5;
                        uint64_t t67 = gm_l14;
                        gm_tfbnciphf3261 t68;
                        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t68 + 0), t66);
                        GM_ST(uint64_t, ((uint8_t *)&t68 + 8), t67);
                        GM_ST(uint8_t, (uint8_t *)&F.l13 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l13 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l13 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l13 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l13 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l13 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l13 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&F.l13 + 16), t68);
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)10ULL));
                    if (!t70) {
                        goto gm_lb7;
                    }
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 8));
                    gm_l15 = t71;
                    gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l28) + 8) + 4));
                    gm_l16 = t72;
                    {
                        void * t73 = F.l0;
                        void * t74 = gm_l10;
                        gm_f_operand_place_cdle4ls((void *)&gm_l31, (void *)NULL, cx, NULL, t73, t74);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l31))[56] != 0);
                    if (t75) {
                        memmove(((uint8_t *)&F.l17), ((uint8_t *)&gm_l31), 56);
                    } else {
                        void * t76 = F.l0;
                        void * t77 = F.l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t76, t77);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    {
                        void * t78 = F.l4;
                        void * t79 = (void *)(((uint8_t *)t78) + 8);
                        uint8_t t80 = gm_l15;
                        gm_t88rcucg1b7uk t81 = gm_l16;
                        gm_tbngattl1fo51 t82;
                        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), t80);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t82 + 4), t81);
                        gm_t53at80pfdus3 t83;
                        memset(&t83, 0, sizeof t83);
                        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)9ULL));
                        GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t83 + 8), t82);
                        gm_t88rcucg1b7uk t84 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t79, &t83);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t84;
                    }
                    {
                        void * t85 = F.l0;
                        void * t86 = F.l0;
                        gm_tb31k5l794t7p t87;
                        GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t87 + 0), ((uint32_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t87 + 8), ((uint64_t)0ULL));
                        gm_tfqmtuprvunod t88;
                        memset(&t88, 0, sizeof t88);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t88 + 16), t87);
                        gm_tlg23kb3oj23s t89;
                        gm_f_project_6hpd48e((void *)&t89, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l17)), &t88);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t90 = gm_l18;
                        void * t91 = F.l1;
                        gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 72));
                        gm_t7b7esi3n9r90 t93;
                        gm_f_load_go4fgkp((void *)&t93, (void *)NULL, cx, NULL, t86, &t89, t90, t92);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t94 = gm_l18;
                        void * t95 = F.l1;
                        gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 72));
                        uint32_t t97 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t85, &t93, t94, t96);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9805qtonbsag t98;
                        GM_ST(uint8_t, (uint8_t *)&t98 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t98 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t98), t97);
                        ((uint8_t *)&t98)[4] = 1;
                        F.l12 = t98;
                    }
                    {
                        void * t99 = F.l0;
                        void * t100 = F.l0;
                        gm_tlg23kb3oj23s t101 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l17));
                        uint64_t t102 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t100);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tb31k5l794t7p t103;
                        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t103 + 0), ((uint32_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t103 + 8), t102);
                        gm_tfqmtuprvunod t104;
                        memset(&t104, 0, sizeof t104);
                        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t104 + 16), t103);
                        gm_tlg23kb3oj23s t105;
                        gm_f_project_6hpd48e((void *)&t105, (void *)NULL, cx, NULL, &t101, &t104);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t106 = gm_l5;
                        void * t107 = F.l1;
                        gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
                        gm_f_load_go4fgkp((void *)&F.l13, (void *)NULL, cx, NULL, t99, &t105, t106, t108);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    void * t109 = F.l0;
                    void * t110 = F.l1;
                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t109, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 473, 5);
            }
gm_lb5:;
            {
                gm_t88rcucg1b7uk t111 = gm_l5;
                gm_tfbnciphf3261 t112;
                GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112 + 0), t111);
                GM_ST(uint64_t, ((uint8_t *)&t112 + 8), ((uint64_t)0ULL));
                gm_t7b7esi3n9r90 t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t113 + 16), t112);
                F.l19 = t113;
            }
            {
                {
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)38ULL));
                    if (!t115) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t117 = (uint8_t)(t116 == ((uint8_t)39ULL));
                    if (!t117) {
                        goto gm_lb11;
                    }
                    void * t118 = F.l0;
                    gm_t88rcucg1b7uk t119 = gm_l5;
                    uint32_t t120 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t118, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t120;
                    {
                        void * t121 = F.l0;
                        uint32_t t122 = gm_l20;
                        gm_t9qkh0rsooc8d t123;
                        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)1ULL));
                        gm_t88rcucg1b7uk t124 = gm_l5;
                        gm_tfbnciphf3261 t125;
                        GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t125 + 0), t124);
                        GM_ST(uint64_t, ((uint8_t *)&t125 + 8), ((uint64_t)1ULL));
                        gm_ts6oqppjh0ba3 t126;
                        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t126 + 1), t123);
                        gm_t7b7esi3n9r90 t127 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l13));
                        gm_t7b7esi3n9r90 t128;
                        GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t128 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t128 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t128 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t128 + 16), t125);
                        gm_t1ctgp8j1v2as t129;
                        GM_ST(uint16_t, (uint8_t *)&t129 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t129 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t122);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t129 + 4), t126);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 16), t127);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 64), t128);
                        void * t130 = F.l1;
                        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t130) + 72));
                        gm_t8658kcqovtpe t132;
                        memset(&t132, 0, sizeof t132);
                        GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t132 + 16), t129);
                        gm_b0_1 t133 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t121, &t132, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t134 = gm_l20;
                        gm_t7b7esi3n9r90 t135;
                        memset(&t135, 0, sizeof t135);
                        GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t135 + 16), t134);
                        F.l19 = t135;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    void * t136 = F.l0;
                    void * t137 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t138, gm_srccujoh2vgr9aq, 495, 29);
                    void * t139 = (void *)(((uint8_t *)t137) + (uintptr_t)((uint64_t)1ULL) * 88);
                    gm_t7b7esi3n9r90 t140;
                    gm_f_expr_tp86bm9((void *)&t140, (void *)NULL, cx, NULL, t136, t139);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l19 = t140;
                    goto gm_lb9;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 488, 5);
            }
gm_lb9:;
            void * t141 = F.l0;
            void * t142 = F.l1;
            gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t142));
            uint32_t t144 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t141, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l21 = t144;
            void * t145 = F.l0;
            void * t146 = F.l4;
            void * t147 = (void *)(((uint8_t *)t146) + 8);
            gm_t88rcucg1b7uk t148 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t149 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t145, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t149;
            {
                void * t150 = F.l0;
                uint32_t t151 = gm_l22;
                gm_ts6oqppjh0ba3 t152;
                GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)10ULL));
                gm_t7b7esi3n9r90 t153 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l19));
                gm_t7b7esi3n9r90 t154 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l13));
                gm_t1ctgp8j1v2as t155;
                GM_ST(uint16_t, (uint8_t *)&t155 + 6, 0);
                GM_ST(uint64_t, (uint8_t *)&t155 + 8, 0);
                GM_ST(uint32_t, ((uint8_t *)&t155 + 0), t151);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t155 + 4), t152);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t155 + 16), t153);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t155 + 64), t154);
                void * t156 = F.l1;
                gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 72));
                gm_t8658kcqovtpe t158;
                memset(&t158, 0, sizeof t158);
                GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t158 + 16), t155);
                gm_b0_1 t159 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t150, &t158, t157);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t160 = F.l0;
            gm_closure t161 = ((gm_closure){(void *)gm_f_fn_m7prcu1, (void *)&F.h, cx});
            gm_b16_8 t162 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t162;
            void * t163 = F.l0;
            gm_closure t164 = ((gm_closure){(void *)gm_f_fn_36v8itm, (void *)&F.h, cx});
            gm_b16_8 t165 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t165;
            {
                void * t166 = F.l0;
                uint32_t t167 = gm_l22;
                gm_b16_8 t168 = gm_l23;
                gm_b16_8 t169 = gm_l24;
                gm_t7b7esi3n9r90 t170;
                memset(&t170, 0, sizeof t170);
                GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t170 + 16), t167);
                gm_t1s3s9ds8udn4 t171;
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t171 + 0), t170);
                GM_ST(gm_b16_8, ((uint8_t *)&t171 + 48), t168);
                GM_ST(gm_b16_8, ((uint8_t *)&t171 + 64), t169);
                void * t172 = F.l1;
                gm_tl874t2atnlh3 t173 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t172) + 72));
                gm_t8658kcqovtpe t174;
                memset(&t174, 0, sizeof t174);
                GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t174 + 16), t171);
                gm_b0_1 t175 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t166, &t174, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t176 = F.l0;
                uint32_t t177 = F.l21;
                gm_tlg23kb3oj23s t178;
                gm_f_local_place_qnf3mb2((void *)&t178, t177);
                void * t179 = F.l1;
                gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t179));
                void * t181 = F.l1;
                gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
                gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t176, &t178, t180, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
        }
gm_lb2:;
        void * t183 = F.l0;
        void * t184 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t183, t184);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_if_expr_bsdi44h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_if_expr_bsdi44h F;
    void * gm_l4; /* c */
    gm_b16_8 gm_l6; /* then */
    gm_b16_8 gm_l7; /* else_ */
    gm_b16_8 gm_l10; /* outer */
    GM_STACK_CHECK(&F, 1071u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    void * t4 = F.l0;
    void * t5 = F.l1;
    gm_t9805qtonbsag t6 = gm_f_result_for_6fafafc((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t6;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)1ULL)));
    if (t8) {
        {
            {
                void * t9 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_srccujoh2vgr9aq, 895, 24);
                uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 24) + 0));
                uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
                if (!t12) {
                    goto gm_lb2;
                }
                void * t13 = GM_LD(void *, ((((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 24) + 8));
                gm_l4 = t13;
                {
                    void * t14 = F.l0;
                    void * t15 = gm_l4;
                    gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t14, t15);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t16 = F.l0;
                gm_closure t17 = ((gm_closure){(void *)gm_f_fn_74evcu5, (void *)&F.h, cx});
                gm_b16_8 t18 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t18;
                void * t19 = F.l0;
                gm_closure t20 = ((gm_closure){(void *)gm_f_fn_a8eu0io, (void *)&F.h, cx});
                gm_b16_8 t21 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t21;
                {
                    void * t22 = F.l0;
                    gm_b16_8 t23 = gm_l6;
                    gm_b16_8 t24 = gm_l7;
                    gm_t7b7esi3n9r90 t25 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                    gm_t1s3s9ds8udn4 t26;
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t26 + 0), t25);
                    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 48), t23);
                    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 64), t24);
                    void * t27 = F.l1;
                    gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
                    gm_t8658kcqovtpe t29;
                    memset(&t29, 0, sizeof t29);
                    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)29ULL));
                    GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t29 + 16), t26);
                    gm_b0_1 t30 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t22, &t29, t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t31 = F.l0;
                    gm_t9805qtonbsag t32 = F.l3;
                    void * t33 = F.l1;
                    gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
                    void * t35 = F.l1;
                    gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 72));
                    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t31, t32, t34, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t37 = F.l0;
    uint32_t t38 = gm_f_label_3373lil(t37);
    F.l8 = t38;
    void * t39 = F.l0;
    uint32_t t40 = gm_f_label_3373lil(t39);
    F.l9 = t40;
    void * t41 = F.l0;
    gm_closure t42 = ((gm_closure){(void *)gm_f_fn_05n2pkm, (void *)&F.h, cx});
    gm_b16_8 t43 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t43;
    {
        void * t44 = F.l0;
        uint32_t t45 = F.l8;
        gm_b16_8 t46 = gm_l10;
        gm_tiilmfd16v3fc t47;
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
        void * t48 = F.l1;
        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
        gm_t8658kcqovtpe t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t50 + 16), t47);
        gm_b0_1 t51 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t44, &t50, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t52 = F.l0;
        gm_t9805qtonbsag t53 = F.l3;
        void * t54 = F.l1;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
        void * t56 = F.l1;
        gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
        gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t52, t53, t55, t57);
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
inline __attribute__((aligned(4))) uint32_t gm_f_in_temp_t7gs0ds(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_in_temp_t7gs0ds F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* x */
    uint32_t gm_l5; /* d */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l4 = t6;
            uint32_t t7 = gm_l4;
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l2;
    uint32_t t10 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t10;
    {
        void * t11 = gm_l0;
        uint32_t t12 = gm_l5;
        gm_t7b7esi3n9r90 t13 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a1));
        gm_tf4lk4crue8vt t14;
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
        GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t14 + 16), t13);
        gm_tl874t2atnlh3 t15 = gm_l3;
        gm_t8658kcqovtpe t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t16 + 16), t14);
        gm_b0_1 t17 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t16, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint32_t t18 = gm_l5;
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
__attribute__((aligned(4))) void gm_f_index_place_3gfu5dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4) {
    struct gm_fr_index_place_3gfu5dn F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* index */
    uint8_t gm_l4; /* checked */
    void * gm_l5; /* s */
    uint64_t gm_l6; /* esz */
    gm_t88rcucg1b7uk gm_l7; /* usize_ty */
    uint64_t gm_l8; /* len */
    uint8_t gm_l11; /* is_const */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint32_t gm_l17; /* t */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    uint64_t gm_l21; /* len */
    GM_STACK_CHECK(&F, 2685u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbccvikg6b96e gm_l24;
    gm_t53at80pfdus3 gm_l25;
    gm_tfonjjajjj3jq gm_l26;
    gm_tfonjjajjj3jq gm_l27;
    gm_t53at80pfdus3 gm_l28;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_l5 = t13;
    {
        void * t14 = gm_l5;
        void * t15 = gm_l1;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
        gm_f_layout_a5ovnrm((void *)&gm_l24, (void *)NULL, cx, NULL, t14, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
    gm_l6 = t17;
    void * t18 = gm_l5;
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    gm_t88rcucg1b7uk t20 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t20;
    {
        void * t21 = gm_l5;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        void * t23 = gm_l2;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
        gm_f_kind_jcnt4hm((void *)&gm_l25, t22, GM_CONV(gm_csguankko4lmhd, t24));
    }
    {
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)11ULL));
            if (!t26) {
                goto gm_lb2;
            }
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
            gm_l8 = t27;
            {
                void * t28 = gm_l0;
                void * t29 = gm_l2;
                gm_f_operand_place_cdle4ls((void *)&gm_l26, (void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l26))[56] != 0);
            if (t30) {
                memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l26), 56);
            } else {
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
            {
                void * t31 = gm_l0;
                void * t32 = gm_l3;
                gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t33 = gm_l4;
            if (t33) {
                void * t34 = gm_l0;
                gm_t88rcucg1b7uk t35 = gm_l7;
                uint64_t t36 = gm_l8;
                gm_tfbnciphf3261 t37;
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37 + 0), t35);
                GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
                void * t38 = gm_l1;
                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                gm_t7b7esi3n9r90 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t40 + 16), t37);
                gm_b0_1 t41 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t34, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l10)), &t40, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t42 = gm_l6;
                gm_t7b7esi3n9r90 t43 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                gm_t592joge1j4uv t44;
                GM_ST(uint64_t, (uint8_t *)&t44 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t44 + 0), t43);
                GM_ST(uint64_t, ((uint8_t *)&t44 + 48), t42);
                gm_tfqmtuprvunod t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t45 + 16), t44);
                gm_tlg23kb3oj23s t46;
                gm_f_project_6hpd48e((void *)&t46, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l9)), &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t46);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)10ULL));
            if (!t48) {
                goto gm_lb3;
            }
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
            gm_l11 = t49;
            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l12 = t50;
            {
                void * t51 = gm_l0;
                void * t52 = gm_l2;
                gm_f_operand_place_cdle4ls((void *)&gm_l27, (void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l27))[56] != 0);
            if (t53) {
                memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l27), 56);
            } else {
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
            {
                void * t54 = gm_l0;
                gm_tb31k5l794t7p t55;
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t55 + 0), ((uint32_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t55 + 8), ((uint64_t)0ULL));
                gm_tfqmtuprvunod t56;
                memset(&t56, 0, sizeof t56);
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t56 + 16), t55);
                gm_tlg23kb3oj23s t57;
                gm_f_project_6hpd48e((void *)&t57, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t58 = gm_l5;
                void * t59 = (void *)(((uint8_t *)t58) + 8);
                uint8_t t60 = gm_l11;
                gm_t88rcucg1b7uk t61 = gm_l12;
                gm_tbngattl1fo51 t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), t60);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t62 + 4), t61);
                gm_t53at80pfdus3 t63;
                memset(&t63, 0, sizeof t63);
                GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)9ULL));
                GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t63 + 8), t62);
                gm_t88rcucg1b7uk t64 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t59, &t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t65 = gm_l1;
                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                gm_f_load_go4fgkp((void *)&F.l14, (void *)NULL, cx, NULL, t54, &t57, t64, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t67 = gm_l0;
                void * t68 = gm_l0;
                gm_tlg23kb3oj23s t69 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l13));
                uint64_t t70 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tb31k5l794t7p t71;
                GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t71 + 0), ((uint32_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t71 + 8), t70);
                gm_tfqmtuprvunod t72;
                memset(&t72, 0, sizeof t72);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t72 + 16), t71);
                gm_tlg23kb3oj23s t73;
                gm_f_project_6hpd48e((void *)&t73, (void *)NULL, cx, NULL, &t69, &t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t74 = gm_l7;
                void * t75 = gm_l1;
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                gm_f_load_go4fgkp((void *)&F.l15, (void *)NULL, cx, NULL, t67, &t73, t74, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t77 = gm_l0;
                void * t78 = gm_l3;
                gm_f_expr_tp86bm9((void *)&F.l16, (void *)NULL, cx, NULL, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t79 = gm_l4;
            if (t79) {
                void * t80 = gm_l0;
                void * t81 = gm_l1;
                gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 72));
                gm_b0_1 t83 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t80, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l16)), &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l15)), t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    uint8_t t85 = (uint8_t)(t84 == ((uint8_t)0ULL));
                    if (!t85) {
                        goto gm_lb5;
                    }
                    uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 16));
                    gm_l17 = t86;
                    {
                        uint32_t t87 = gm_l17;
                        uint64_t t88 = gm_l6;
                        gm_t7b7esi3n9r90 t89 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t592joge1j4uv t90;
                        GM_ST(uint64_t, (uint8_t *)&t90 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t90 + 0), t89);
                        GM_ST(uint64_t, ((uint8_t *)&t90 + 48), t88);
                        gm_tu78tssgij60g t91;
                        memset(&t91, 0, sizeof t91);
                        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)4ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t91 + 8), t87);
                        gm_tfqmtuprvunod t92;
                        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t92 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t92 + 16), t90);
                        gm_b16_8 t93 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t92);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tlg23kb3oj23s t94;
                        GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t94 + 0), t91);
                        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 40), t93);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t94);
                        ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb5:;
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
        {
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)9ULL));
            if (!t96) {
                goto gm_lb6;
            }
            {
                void * t97 = gm_l0;
                void * t98 = gm_l2;
                gm_f_expr_tp86bm9((void *)&F.l18, (void *)NULL, cx, NULL, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t99 = gm_l0;
                void * t100 = gm_l3;
                gm_f_expr_tp86bm9((void *)&F.l19, (void *)NULL, cx, NULL, t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t101 = gm_l0;
                void * t102 = gm_l2;
                gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t102));
                void * t104 = gm_l2;
                gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 72));
                uint32_t t106 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t101, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l18)), t103, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t107 = gm_l6;
                gm_t7b7esi3n9r90 t108 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l19));
                gm_t592joge1j4uv t109;
                GM_ST(uint64_t, (uint8_t *)&t109 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t109 + 0), t108);
                GM_ST(uint64_t, ((uint8_t *)&t109 + 48), t107);
                gm_tu78tssgij60g t110;
                memset(&t110, 0, sizeof t110);
                GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)4ULL));
                GM_ST(uint32_t, ((uint8_t *)&t110 + 8), t106);
                gm_tfqmtuprvunod t111;
                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t111 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t111 + 16), t109);
                gm_b16_8 t112 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tlg23kb3oj23s t113;
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t113 + 0), t110);
                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 40), t112);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t113);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb6:;
        {
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)8ULL));
            if (!t115) {
                goto gm_lb7;
            }
            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l20 = t116;
            {
                {
                    {
                        void * t117 = gm_l5;
                        void * t118 = (void *)(((uint8_t *)t117) + 8);
                        gm_t88rcucg1b7uk t119 = gm_l20;
                        gm_f_kind_jcnt4hm((void *)&gm_l28, t118, GM_CONV(gm_csguankko4lmhd, t119));
                    }
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)11ULL));
                    if (!t121) {
                        goto gm_lb9;
                    }
                    uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                    gm_l21 = t122;
                    {
                        void * t123 = gm_l0;
                        void * t124 = gm_l2;
                        gm_f_expr_tp86bm9((void *)&F.l22, (void *)NULL, cx, NULL, t123, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t125 = gm_l0;
                        void * t126 = gm_l3;
                        gm_f_expr_tp86bm9((void *)&F.l23, (void *)NULL, cx, NULL, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t127 = gm_l4;
                    if (t127) {
                        void * t128 = gm_l0;
                        gm_t88rcucg1b7uk t129 = gm_l7;
                        uint64_t t130 = gm_l21;
                        gm_tfbnciphf3261 t131;
                        GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t131 + 0), t129);
                        GM_ST(uint64_t, ((uint8_t *)&t131 + 8), t130);
                        void * t132 = gm_l1;
                        gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
                        gm_t7b7esi3n9r90 t134;
                        GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t134 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t134 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t134 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t134 + 16), t131);
                        gm_b0_1 t135 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t128, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l23)), &t134, t133);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t136 = gm_l0;
                        void * t137 = gm_l2;
                        gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t137));
                        void * t139 = gm_l2;
                        gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t139) + 72));
                        uint32_t t141 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t136, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l22)), t138, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t142 = gm_l6;
                        gm_t7b7esi3n9r90 t143 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l23));
                        gm_t592joge1j4uv t144;
                        GM_ST(uint64_t, (uint8_t *)&t144 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t144 + 0), t143);
                        GM_ST(uint64_t, ((uint8_t *)&t144 + 48), t142);
                        gm_tu78tssgij60g t145;
                        memset(&t145, 0, sizeof t145);
                        GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)4ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t145 + 8), t141);
                        gm_tfqmtuprvunod t146;
                        GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t146 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t146 + 16), t144);
                        gm_b16_8 t147 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t146);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tlg23kb3oj23s t148;
                        GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t148 + 0), t145);
                        GM_ST(gm_b16_8, ((uint8_t *)&t148 + 40), t147);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t148);
                        ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb9:;
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
gm_lb8:;
        }
gm_lb7:;
        {
            memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
            goto gm_fdone;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 611, 5);
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
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8_c(gm_tbci5ospk5nnn a0) { return gm_f_int_bits_6bio9a8(&a0); }
inline __attribute__((aligned(4))) void gm_f_int_value_ttu5547(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_value_ttu5547 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l3; /* v */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        memset(&(*(gm_tf8kfhcnval78 *)gm_out), 0, sizeof (*(gm_tf8kfhcnval78 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, ((uint8_t *)t8));
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t12 = (t11 > ((uint64_t)8ULL));
    if (t12) {
        gm_t88rcucg1b7uk t13 = gm_l1;
        unsigned __int128 t14 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
        gm_tl5jbgvjjt15g t15;
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 0), t13);
        GM_ST(unsigned __int128, ((uint8_t *)&t15 + 16), t14);
        gm_t7b7esi3n9r90 t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t16 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tl5jbgvjjt15g, ((uint8_t *)&t16 + 16), t15);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out)), t16);
        ((uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out))[48] = 1;
        goto gm_fdone;
    }
    {
        gm_tf5srh4mp4b3i t17 = gm_f_int_bits_6bio9a8(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
        gm_l5 = t17;
    }
    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t18) {
        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l3 = t19;
    } else {
        memset(&(*(gm_tf8kfhcnval78 *)gm_out), 0, sizeof (*(gm_tf8kfhcnval78 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        uint64_t t22 = gm_l3;
        gm_f_word_1sfojus((void *)&(*(gm_tf8kfhcnval78 *)gm_out), (void *)NULL, cx, NULL, t20, t21, t22);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bits_pbijkuf(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bits_pbijkuf F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    uint8_t gm_l5;
    {
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, ((uint8_t *)t9));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t11, GM_CONV(gm_csguankko4lmhd, t12));
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)17ULL));
            if (!t14) {
                goto gm_lb2;
            }
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t15;
            {
                {
                    void * t16 = gm_l0;
                    void * t17 = GM_LD(void *, ((uint8_t *)t16));
                    void * t18 = GM_LD(void *, (((uint8_t *)t17) + 1088));
                    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 1088) + 8));
                    uint32_t t20 = gm_l2;
                    uint64_t t21 = (uint64_t)t20;
                    if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_srccujoh2vgr9aq, 602, 21);
                    uint8_t t22 = GM_LD(uint8_t, (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                    if (!t23) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 8), 48);
                    {
                        {
                            uint8_t t24 = (uint8_t)(((((uint8_t *)&F.l3) + 40))[4] != 0);
                            if (t24) {
                                goto gm_lb6;
                            }
                            gm_l5 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l5 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 603, 12);
                    }
gm_lb5:;
                    uint8_t t25 = gm_l5;
                    F.ret = t25;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_place_expr_6llket3(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_place_expr_6llket3 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    void * t16 = gm_l1;
    {
        {
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)15ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)16ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)17ULL));
                    if (!t22) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)18ULL));
                    if (!t24) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)14ULL));
                    if (!t26) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)19ULL));
            if (!t28) {
                goto gm_lb9;
            }
            void * t29 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l2 = t29;
            void * t30 = gm_l0;
            void * t31 = gm_l2;
            uint8_t t32 = gm_f_is_place_expr_6llket3(t30, t31);
            gm_l4 = t32;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)20ULL));
            if (!t34) {
                goto gm_lb10;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l3 = t35;
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            void * t38 = gm_l3;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t38));
            gm_f_kind_jcnt4hm((void *)&gm_l7, t37, GM_CONV(gm_csguankko4lmhd, t39));
            {
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)11ULL));
                    if (!t41) {
                        goto gm_lb12;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb11;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 589, 30);
            }
gm_lb11:;
            uint8_t t42 = gm_l6;
            if (t42) {
                void * t43 = gm_l0;
                void * t44 = gm_l3;
                uint8_t t45 = gm_f_is_place_expr_6llket3(t43, t44);
                gm_l5 = t45;
            } else {
                gm_l5 = ((uint8_t)1);
            }
            uint8_t t46 = gm_l5;
            gm_l4 = t46;
            goto gm_lb1;
        }
gm_lb10:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 586, 58);
    }
gm_lb1:;
    uint8_t t47 = gm_l4;
    F.ret = t47;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_leave_ehrof6p(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_leave_ehrof6p F;
    GM_STACK_CHECK(&F, 918u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tf8kfhcnval78 gm_l1;
    gm_tbccvikg6b96e gm_l2;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 16));
    uint8_t t3 = ((t2 == ((uint32_t)0ULL)));
    if (t3) {
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        gm_t7b7esi3n9r90 t5 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
        gm_tf8kfhcnval78 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t6), t5);
        ((uint8_t *)&t6)[48] = 1;
        void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 72));
        gm_t8658kcqovtpe t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)32ULL));
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t9 + 16), t6);
        gm_b0_1 t10 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t4, &t9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 16));
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
        gm_t7pnmbsgd9mnq t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t15 + 4), t12);
        gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        uint8_t t17 = (t16 > ((uint64_t)0ULL));
        if (t17) {
            gm_t7b7esi3n9r90 t18 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
            GM_ST(uint8_t, (uint8_t *)&gm_l1 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l1 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l1 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l1 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&gm_l1), t18);
            ((uint8_t *)&gm_l1)[48] = 1;
        } else {
            memset(&gm_l1, 0, sizeof gm_l1);
        }
        gm_tf8kfhcnval78 t19 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&gm_l1));
        gm_t7dcjsiqi95h3 t20;
        GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t20 + 0), t15);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t20 + 16), t19);
        void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
        gm_t8658kcqovtpe t23;
        memset(&t23, 0, sizeof t23);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)39ULL));
        GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t23 + 16), t20);
        gm_b0_1 t24 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t23, t22);
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
inline __attribute__((aligned(4))) void gm_f_load_go4fgkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_load_go4fgkp F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    uint32_t t4 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t4;
    {
        void * t5 = gm_l0;
        uint32_t t6 = gm_l4;
        gm_tlg23kb3oj23s t7 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
        gm_tlpcrb3vu6o2d t8;
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t6);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t8 + 8), t7);
        gm_tl874t2atnlh3 t9 = gm_l3;
        gm_t8658kcqovtpe t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t10 + 16), t8);
        gm_b0_1 t11 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t10, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t12 = gm_l4;
        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t12);
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
extern __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0);
__attribute__((aligned(4))) gm_tlg23kb3oj23s gm_f_local_place_qnf3mb2_c(uint32_t a0) { gm_tlg23kb3oj23s gm_r; gm_f_local_place_qnf3mb2(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_local_target_vvq8lnf_c(void * a0, gm_te9hrk0u35rev a1) { gm_t7isn7qnmqh8i gm_r; gm_f_local_target_vvq8lnf(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_logic_9o6bp90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3, void * a4) {
    struct gm_fr_logic_9o6bp90 F;
    void * gm_l3; /* l */
    gm_b16_8 gm_l7; /* rest */
    gm_b16_8 gm_l8; /* decided */
    GM_STACK_CHECK(&F, 1069u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    F.l4 = a4;
    void * t2 = F.l0;
    void * t3 = F.l1;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t3));
    uint32_t t5 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t5;
    {
        void * t6 = F.l0;
        void * t7 = gm_l3;
        gm_f_expr_tp86bm9((void *)&F.l6, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t8 = F.l0;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_drdvs2i, (void *)&F.h, cx});
    gm_b16_8 t10 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t10;
    void * t11 = F.l0;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_cquk74j, (void *)&F.h, cx});
    gm_b16_8 t13 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t13;
    uint8_t t14 = F.l2;
    if (t14) {
        void * t15 = F.l0;
        gm_b16_8 t16 = gm_l7;
        gm_b16_8 t17 = gm_l8;
        gm_t7b7esi3n9r90 t18 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l6));
        gm_t1s3s9ds8udn4 t19;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t19 + 0), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 48), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 64), t17);
        void * t20 = F.l1;
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
        gm_t8658kcqovtpe t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t22 + 16), t19);
        gm_b0_1 t23 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t15, &t22, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t24 = F.l0;
        gm_b16_8 t25 = gm_l8;
        gm_b16_8 t26 = gm_l7;
        gm_t7b7esi3n9r90 t27 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l6));
        gm_t1s3s9ds8udn4 t28;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t28 + 0), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 48), t25);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 64), t26);
        void * t29 = F.l1;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
        gm_t8658kcqovtpe t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t31 + 16), t28);
        gm_b0_1 t32 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t24, &t31, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = F.l0;
        uint32_t t34 = F.l5;
        gm_tlg23kb3oj23s t35;
        gm_f_local_place_qnf3mb2((void *)&t35, t34);
        void * t36 = F.l1;
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t36));
        void * t38 = F.l1;
        gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t33, &t35, t37, t39);
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
inline __attribute__((aligned(4))) void gm_f_loop_expr_k6v92f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_loop_expr_k6v92f2 F;
    gm_t9805qtonbsag gm_l3; /* res */
    gm_b16_8 gm_l7; /* outer */
    GM_STACK_CHECK(&F, 627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t5 = F.l0;
    void * t6 = F.l1;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t6));
    gm_t9805qtonbsag t8 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    void * t9 = F.l0;
    uint32_t t10 = gm_f_label_3373lil(t9);
    F.l4 = t10;
    void * t11 = F.l0;
    uint32_t t12 = gm_f_label_3373lil(t11);
    F.l5 = t12;
    void * t13 = F.l0;
    uint32_t t14 = gm_f_label_3373lil(t13);
    F.l6 = t14;
    {
        void * t15 = F.l0;
        void * t16 = (void *)(((uint8_t *)t15) + 144);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
        uint32_t t18 = F.l4;
        uint32_t t19 = F.l6;
        gm_t9805qtonbsag t20 = gm_l3;
        gm_tp4rm55bg84ue t21;
        GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t17);
        GM_ST(uint32_t, ((uint8_t *)&t21 + 4), t18);
        GM_ST(uint32_t, ((uint8_t *)&t21 + 8), t19);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t21 + 12), t20);
        gm_tftb1odc739t5 t22 = gm_f_push_e2qt8as(t16, &t21);
        gm_l8 = t22;
    }
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccujoh2vgr9aq, 969, 5);
    {
        for (;;) {
            void * t25 = F.l0;
            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t25) + 192) + 8));
            uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            uint64_t t28 = (uint64_t)t27;
            uint8_t t29 = (t26 <= t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                void * t30 = F.l0;
                void * t31 = (void *)(((uint8_t *)t30) + 192);
                gm_t9805qtonbsag t32;
                memset(&t32, 0, sizeof t32);
                gm_tftb1odc739t5 t33 = gm_f_push_r4haklv(t31, t32);
                gm_l9 = t33;
                uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccujoh2vgr9aq, 970, 52);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t35 = F.l0;
    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 192));
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 192) + 8));
    uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint64_t t39 = (uint64_t)t38;
    if ((uintptr_t)t39 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t39, t37, gm_srccujoh2vgr9aq, 971, 5);
    gm_t9805qtonbsag t40 = gm_l3;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t36) + (uintptr_t)t39 * 8), t40);
    void * t41 = F.l0;
    gm_closure t42 = ((gm_closure){(void *)gm_f_fn_cccid3f, (void *)&F.h, cx});
    gm_b16_8 t43 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t43;
    {
        void * t44 = F.l0;
        uint32_t t45 = F.l4;
        gm_b16_8 t46 = gm_l7;
        gm_tiilmfd16v3fc t47;
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
        void * t48 = F.l1;
        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
        gm_t8658kcqovtpe t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t50 + 16), t47);
        gm_b0_1 t51 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t44, &t50, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t52 = F.l0;
        gm_t9805qtonbsag t53 = gm_l3;
        void * t54 = F.l1;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
        void * t56 = F.l1;
        gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
        gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t52, t53, t55, t57);
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
inline __attribute__((aligned(4))) void gm_f_loop_labels_uvqimfl(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_loop_labels_uvqimfl F;
    void * gm_l0; /* lw */
    uint32_t gm_l1; /* tl */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tp4rm55bg84ue gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 144));
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
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 20), 20);
                        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t16 = gm_l1;
                        uint8_t t17 = ((t15 == t16));
                        if (t17) {
                            gm_tp4rm55bg84ue t18 = GM_LD(gm_tp4rm55bg84ue, ((uint8_t *)&gm_l4));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t7isn7qnmqh8i *)gm_out) + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t7isn7qnmqh8i *)gm_out) + 22, 0);
                            GM_ST(gm_tp4rm55bg84ue, ((uint8_t *)&(*(gm_t7isn7qnmqh8i *)gm_out)), t18);
                            ((uint8_t *)&(*(gm_t7isn7qnmqh8i *)gm_out))[20] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 952, 5);
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
    memset(&(*(gm_t7isn7qnmqh8i *)gm_out), 0, sizeof (*(gm_t7isn7qnmqh8i *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_loop_labels_uvqimfl_c(void * a0, uint32_t a1) { gm_t7isn7qnmqh8i gm_r; gm_f_loop_labels_uvqimfl(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_lower_rh7s5j6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_lower_rh7s5j6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* s */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_tbccvikg6b96e gm_l9; /* lay */
    gm_t88rcucg1b7uk gm_l10; /* ret */
    void * gm_l11; /* b */
    GM_STACK_CHECK(&F, 1645u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5im2v50m3hk6 gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_l2 = t13;
    {
        void * t14 = gm_l2;
        uint32_t t15 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l3, t14, t15);
    }
    {
        void * t16 = gm_l2;
        uint32_t t17 = gm_l1;
        gm_trqfvmjtt4sij t18;
        gm_f_list_a3ut5mo((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t19;
        gm_f_list_nof7vfj((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t20;
        gm_f_list_0ngbo25((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t21;
        gm_f_list_gaq8h4q((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t22;
        gm_f_list_rb5mlsd((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5lf9i3n1r3co t23;
        memset(&t23, 0, sizeof t23);
        gm_tl905bk085r8i t24;
        GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 140, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 188, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 236, 0);
        GM_ST(uint8_t, (uint8_t *)&t24 + 265, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 266, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 268, 0);
        GM_ST(void *, ((uint8_t *)&t24 + 0), t16);
        GM_ST(uint32_t, ((uint8_t *)&t24 + 8), t17);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 16), t18);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 56), t19);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 96), t20);
        GM_ST(uint32_t, ((uint8_t *)&t24 + 136), ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 144), t21);
        GM_ST(uint32_t, ((uint8_t *)&t24 + 184), ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 192), t22);
        GM_ST(uint32_t, ((uint8_t *)&t24 + 232), ((uint32_t)0ULL));
        GM_ST(gm_t5lf9i3n1r3co, ((uint8_t *)&t24 + 240), t23);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 264), ((uint8_t)0));
        F.l4 = t24;
    }
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    gm_l5 = t26;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l6;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t30 = gm_l6;
                    void * t31 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t33 = gm_l6;
                    gm_tsoie1ppgdvcg t34 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t31) + (uintptr_t)t33 * 40));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l14 + 0), t30);
                    GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&gm_l14 + 8), t34);
                }
                {
                    {
                        uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l7 = t35;
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l14) + 8), 40);
                        }
                        {
                            void * t36 = gm_l2;
                            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                            gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t38 = (void *)(((uint8_t *)&F.l4) + 16);
                            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                            uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            uint64_t t44 = gm_l7;
                            uint32_t t45 = (uint32_t)t44;
                            gm_tf5srh4mp4b3i t46 = gm_f_index_of_41b084m(t43, t45);
                            gm_l16 = t46;
                            {
                                {
                                    uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                                    if (t47) {
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
                                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 32, 81);
                            }
gm_lb6:;
                            uint8_t t48 = gm_l15;
                            gm_t0o0svomecjg6 t49;
                            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t49 + 28, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t49 + 0), t39);
                            GM_ST(uint64_t, ((uint8_t *)&t49 + 8), t40);
                            GM_ST(uint64_t, ((uint8_t *)&t49 + 16), t41);
                            GM_ST(uint8_t, ((uint8_t *)&t49 + 24), t48);
                            GM_ST(uint8_t, ((uint8_t *)&t49 + 25), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t49 + 26), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t49 + 27), ((uint8_t)0));
                            gm_tftb1odc739t5 t50 = gm_f_push_u8i76or(t38, &t49);
                            gm_l17 = t50;
                        }
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccujoh2vgr9aq, 32, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 30, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l6;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l6 = t55;
        }
    }
gm_lb1:;
    void * t56 = gm_l0;
    uint32_t t57 = gm_l1;
    uint32_t t58 = gm_l1;
    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
    gm_f_mark_captured_lpp77bm(t56, t57, ((uint32_t)1ULL), t58, t59);
    void * t60 = gm_l2;
    void * t61 = GM_LD(void *, (((uint8_t *)t60) + 344));
    uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 344) + 8));
    uint32_t t63 = gm_l1;
    uint64_t t64 = (uint64_t)t63;
    if ((uintptr_t)t64 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t64, t62, gm_srccujoh2vgr9aq, 35, 12);
    gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t61) + (uintptr_t)t64 * 136) + 56));
    gm_l10 = t65;
    {
        {
            uint8_t t66 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l3) + 40)) != 0);
            if (!t66) {
                goto gm_lb10;
            }
            void * t67 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            gm_l11 = t67;
            void * t68 = (void *)((uint8_t *)&F.l4);
            void * t69 = gm_l11;
            gm_f_expr_tp86bm9((void *)&F.l12, (void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t70 = (void *)((uint8_t *)&F.l4);
            void * t71 = gm_l11;
            uint8_t t72 = gm_f_diverges_p3okmol(t70, t71);
            uint8_t t73 = (uint8_t)!t72;
            if (t73) {
                void * t74 = (void *)((uint8_t *)&F.l4);
                gm_t7b7esi3n9r90 t75 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l12));
                gm_tf8kfhcnval78 t76;
                GM_ST(uint8_t, (uint8_t *)&t76 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t76 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t76 + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&t76 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t76), t75);
                ((uint8_t *)&t76)[48] = 1;
                void * t77 = gm_l11;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                gm_t8658kcqovtpe t79;
                memset(&t79, 0, sizeof t79);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)32ULL));
                GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t79 + 16), t76);
                gm_b0_1 t80 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t74, &t79, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb9;
        }
gm_lb10:;
    }
gm_lb9:;
    {
        uint32_t t81 = gm_l1;
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 56));
        gm_t88rcucg1b7uk t84 = gm_l10;
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 96));
        uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 136));
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 144));
        uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 184));
        gm_tqh25f25b69k5 t89;
        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 44, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 68, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 92, 0);
        GM_ST(uint32_t, ((uint8_t *)&t89 + 0), t81);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 8), t82);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 24), t83);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89 + 40), t84);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 48), t85);
        GM_ST(uint32_t, ((uint8_t *)&t89 + 64), t86);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 72), t87);
        GM_ST(uint32_t, ((uint8_t *)&t89 + 88), t88);
        F.l13 = t89;
    }
    uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 232));
    uint8_t t91 = (t90 > ((uint32_t)0ULL));
    if (t91) {
        void * t92 = gm_l2;
        void * t93 = (void *)((uint8_t *)&F.l13);
        uint32_t t94 = gm_f_elide_checks_s9bgk65((void *)NULL, cx, NULL, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l13), 96);
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
inline __attribute__((aligned(4))) void gm_f_mark_addressed_t8qojin(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_addressed_t8qojin F;
    void * gm_l0; /* lw */
    void * gm_l1; /* p */
    uint32_t gm_l2; /* l */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t9) + 8));
            gm_l2 = t12;
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 40));
            gm_l3 = t14;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t17 = (t15 < t16);
                    if (!t17) {
                        goto gm_lb3;
                    }
                    {
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t20 = gm_l4;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l5), (((uint8_t *)t18) + (uintptr_t)t20 * 80), 80);
                                }
                                {
                                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                    uint8_t t22 = (uint8_t)((t21 == ((uint8_t)3ULL)));
                                    gm_tfqmtuprvunod t23 = GM_LD(gm_tfqmtuprvunod, ((uint8_t *)&F.l5));
                                    gm_tfqmtuprvunod t24;
                                    memset(&t24, 0, sizeof t24);
                                    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)3ULL));
                                    if (t22) {
                                        F.ret = GM_ZERO(gm_b0_1);
                                        goto gm_fret;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 658, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t25 = gm_l4;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l4 = t28;
                }
            }
gm_lb3:;
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 16));
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 16) + 8));
            uint32_t t32 = gm_l2;
            uint64_t t33 = (uint64_t)t32;
            if ((uintptr_t)t33 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t33, t31, gm_srccujoh2vgr9aq, 661, 5);
            GM_ST(uint8_t, ((((uint8_t *)t30) + (uintptr_t)t33 * 32) + 25), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mark_captured_lpp77bm(void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_b16_8 a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_captured_lpp77bm F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    uint32_t gm_l3; /* outer */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* g */
    void * gm_l8; /* b */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    F.l4 = a4;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
    uint32_t t12 = gm_l3;
    uint64_t t13 = (uint64_t)t12;
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srccujoh2vgr9aq, 674, 14);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t10) + (uintptr_t)t13 * 40));
    gm_l5 = t14;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l6;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t21 = gm_l6;
                {
                    {
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                        gm_l7 = t22;
                        {
                            {
                                void * t23 = gm_l0;
                                void * t24 = GM_LD(void *, ((uint8_t *)t23));
                                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 384));
                                uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 384) + 8));
                                uint32_t t27 = gm_l7;
                                uint64_t t28 = (uint64_t)t27;
                                if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srccujoh2vgr9aq, 675, 17);
                                uint8_t t29 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t25) + (uintptr_t)t28 * 64) + 40)) != 0);
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                void * t30 = GM_LD(void *, ((((uint8_t *)t25) + (uintptr_t)t28 * 64) + 40));
                                gm_l8 = t30;
                                void * t31 = gm_l8;
                                gm_closure t32 = ((gm_closure){(void *)gm_f_fn_mh72upo, (void *)&F.h, cx});
                                gm_f_visit_ajtfk2n(t31, t32);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t33 = gm_l0;
                        uint32_t t34 = gm_l1;
                        uint32_t t35 = F.l2;
                        uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
                        if (o37) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 682, 30);
                        uint32_t t38 = r36;
                        uint32_t t39 = gm_l7;
                        gm_b16_8 t40 = F.l4;
                        gm_f_mark_captured_lpp77bm(t33, t34, t38, t39, t40);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 674, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l6;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l6 = t44;
        }
    }
    goto gm_ls45;
gm_ll15:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls45:;
gm_lb1:;
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
extern __attribute__((aligned(4))) void gm_f_mark_root_b973vvi(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_match_expr_rfas5u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_match_expr_rfas5u2 F;
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l7; /* body */
    GM_STACK_CHECK(&F, 533u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_tfonjjajjj3jq gm_l8;
    void * t2 = F.l0;
    void * t3 = F.l1;
    gm_t9805qtonbsag t4 = gm_f_result_for_6fafafc((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t4;
    {
        void * t5 = F.l0;
        void * t6 = gm_l2;
        gm_f_operand_place_cdle4ls((void *)&gm_l8, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l8))[56] != 0);
    if (t7) {
        memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l8), 56);
    } else {
        void * t8 = F.l0;
        void * t9 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t10 = F.l0;
    uint32_t t11 = gm_f_label_3373lil(t10);
    F.l6 = t11;
    void * t12 = F.l0;
    gm_closure t13 = ((gm_closure){(void *)gm_f_fn_etfgial, (void *)&F.h, cx});
    gm_b16_8 t14 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t14;
    {
        void * t15 = F.l0;
        uint32_t t16 = F.l6;
        gm_b16_8 t17 = gm_l7;
        gm_tiilmfd16v3fc t18;
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
        void * t19 = F.l1;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
        gm_t8658kcqovtpe t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t21 + 16), t18);
        gm_b0_1 t22 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t15, &t21, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t23 = F.l0;
        gm_t9805qtonbsag t24 = F.l4;
        void * t25 = F.l1;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t25));
        void * t27 = F.l1;
        gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
        gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t23, t24, t26, t28);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_operand_place_cdle4ls(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_operand_place_cdle4ls F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 635u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = gm_l1;
            uint8_t t7 = gm_f_is_place_expr_6llket3(t5, t6);
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = gm_l0;
            void * t9 = gm_l1;
            uint8_t t10 = gm_f_bits_field_cdqu5on(t8, t9);
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l0;
            void * t13 = gm_l1;
            gm_f_place_oohr45n((void *)&(*(gm_tfonjjajjj3jq *)gm_out), (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_f_expr_tp86bm9((void *)&F.l2, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t16 = gm_l0;
    void * t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    uint32_t t19 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t19;
    {
        void * t20 = gm_l0;
        uint32_t t21 = gm_l3;
        gm_tlg23kb3oj23s t22;
        gm_f_local_place_qnf3mb2((void *)&t22, t21);
        gm_t7b7esi3n9r90 t23 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
        gm_t6fp3q71vjrmk t24;
        GM_ST(uint64_t, (uint8_t *)&t24 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t24 + 0), t22);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t24 + 64), t23);
        void * t25 = gm_l1;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
        gm_t8658kcqovtpe t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t27 + 16), t24);
        gm_b0_1 t28 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t27, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t29 = gm_l3;
        gm_tlg23kb3oj23s t30;
        gm_f_local_place_qnf3mb2((void *)&t30, t29);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t30);
        ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_od1h9r0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3) {
    struct gm_fr_pattern_od1h9r0 F;
    void * gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* bool_ty */
    uint32_t gm_l6; /* l */
    void * gm_l8; /* c */
    uint32_t gm_l11; /* d */
    uint8_t gm_l14; /* inclusive */
    uint32_t gm_l18; /* ge */
    uint32_t gm_l19; /* le */
    uint32_t gm_l20; /* d */
    void * gm_l21; /* q */
    uint32_t gm_l22; /* d */
    void * gm_l23; /* q */
    void * gm_l24; /* q */
    uint32_t gm_l25; /* d */
    uint32_t gm_l26; /* v */
    void * gm_l27; /* payload */
    uint32_t gm_l28; /* d */
    void * gm_l29; /* q */
    gm_b16_8 gm_l30; /* fs */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l34; /* x */
    uint32_t gm_l35; /* d */
    uint32_t gm_l36; /* l */
    gm_b16_8 gm_l37; /* offs */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l40; /* x */
    uint64_t gm_l41; /* off */
    gm_b16_8 gm_l44; /* body */
    GM_STACK_CHECK(&F, 9595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    F.l3 = a3;
    gm_tbccvikg6b96e gm_l45;
    gm_tf8kfhcnval78 gm_l46;
    gm_tf8kfhcnval78 gm_l47;
    gm_ts6oqppjh0ba3 gm_l48;
    uint8_t gm_l49;
    gm_tbccvikg6b96e gm_l50;
    {
        void * t31 = F.l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        gm_l4 = t32;
        void * t33 = gm_l4;
        void * t34 = (void *)(((uint8_t *)t33) + 8);
        gm_t88rcucg1b7uk t35 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t35;
        void * t36 = F.l1;
        {
            {
                {
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                    if (!t38) {
                        goto gm_lb2;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
                    if (!t40) {
                        goto gm_lb3;
                    }
                    uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l6 = t41;
                    {
                        void * t42 = gm_l4;
                        void * t43 = F.l1;
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t43));
                        gm_f_layout_a5ovnrm((void *)&gm_l45, (void *)NULL, cx, NULL, t42, t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)&gm_l45));
                    uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
                    if (t46) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t47 = F.l0;
                        void * t48 = F.l1;
                        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
                        void * t50 = F.l1;
                        gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 120));
                        gm_tlg23kb3oj23s t52 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_f_load_go4fgkp((void *)&F.l7, (void *)NULL, cx, NULL, t47, &t52, t49, t51);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t53 = F.l0;
                        uint32_t t54 = gm_l6;
                        gm_tlg23kb3oj23s t55;
                        gm_f_local_place_qnf3mb2((void *)&t55, t54);
                        gm_t7b7esi3n9r90 t56 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                        gm_t6fp3q71vjrmk t57;
                        GM_ST(uint64_t, (uint8_t *)&t57 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t57 + 0), t55);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t57 + 64), t56);
                        void * t58 = F.l1;
                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 120));
                        gm_t8658kcqovtpe t60;
                        memset(&t60, 0, sizeof t60);
                        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t60 + 16), t57);
                        gm_b0_1 t61 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t53, &t60, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)2ULL));
                    if (!t63) {
                        goto gm_lb4;
                    }
                    void * t64 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                    gm_l8 = t64;
                    {
                        void * t65 = F.l0;
                        void * t66 = F.l1;
                        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t66));
                        void * t68 = F.l1;
                        gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 120));
                        gm_tlg23kb3oj23s t70 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_f_load_go4fgkp((void *)&F.l9, (void *)NULL, cx, NULL, t65, &t70, t67, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t71 = F.l0;
                        void * t72 = gm_l8;
                        gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t71, t72);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t73 = F.l0;
                    gm_t88rcucg1b7uk t74 = gm_l5;
                    uint32_t t75 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t75;
                    {
                        void * t76 = F.l0;
                        uint32_t t77 = gm_l11;
                        gm_t7b7esi3n9r90 t78 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l9));
                        gm_t7b7esi3n9r90 t79 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                        gm_tr6ll1dg9imrl t80;
                        GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t80 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t77);
                        GM_ST(uint8_t, ((uint8_t *)&t80 + 4), ((uint8_t)0));
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t80 + 16), t78);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t80 + 64), t79);
                        void * t81 = F.l1;
                        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 120));
                        gm_t8658kcqovtpe t83;
                        memset(&t83, 0, sizeof t83);
                        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t83 + 16), t80);
                        gm_b0_1 t84 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t76, &t83, t82);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t85 = F.l0;
                        uint32_t t86 = gm_l11;
                        uint32_t t87 = F.l3;
                        gm_t7b7esi3n9r90 t88;
                        memset(&t88, 0, sizeof t88);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t88 + 16), t86);
                        gm_tejc6vv5sqno9 t89;
                        GM_ST(uint8_t, (uint8_t *)&t89 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t89 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t89 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t89 + 0), t88);
                        GM_ST(uint8_t, ((uint8_t *)&t89 + 48), ((uint8_t)1));
                        GM_ST(uint32_t, ((uint8_t *)&t89 + 52), t87);
                        void * t90 = F.l1;
                        gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 120));
                        gm_t8658kcqovtpe t92;
                        memset(&t92, 0, sizeof t92);
                        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t92 + 16), t89);
                        gm_b0_1 t93 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t85, &t92, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)3ULL));
                    if (!t95) {
                        goto gm_lb5;
                    }
                    {
                        memmove(((uint8_t *)&F.l12), ((((uint8_t *)t36) + 8) + 8), 48);
                    }
                    {
                        memmove(((uint8_t *)&F.l13), (((((uint8_t *)t36) + 8) + 8) + 48), 48);
                    }
                    uint8_t t96 = GM_LD(uint8_t, (((((uint8_t *)t36) + 8) + 8) + 96));
                    gm_l14 = t96;
                    {
                        void * t97 = F.l0;
                        void * t98 = F.l1;
                        gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t98));
                        void * t100 = F.l1;
                        gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 120));
                        gm_tlg23kb3oj23s t102 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_f_load_go4fgkp((void *)&F.l15, (void *)NULL, cx, NULL, t97, &t102, t99, t101);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t103 = F.l0;
                        void * t104 = F.l1;
                        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t104));
                        gm_tbci5ospk5nnn t106 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l12));
                        gm_f_int_value_ttu5547((void *)&gm_l46, (void *)NULL, cx, NULL, t103, t105, &t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l46))[48] != 0);
                    if (t107) {
                        memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l46), 48);
                    } else {
                        void * t108 = F.l0;
                        void * t109 = F.l1;
                        uint32_t t110 = F.l3;
                        gm_b0_1 t111 = gm_f_escape_pattern_6lpj7tc((void *)NULL, cx, NULL, t108, t109, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t112 = F.l0;
                        void * t113 = F.l1;
                        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t113));
                        gm_tbci5ospk5nnn t115 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l13));
                        gm_f_int_value_ttu5547((void *)&gm_l47, (void *)NULL, cx, NULL, t112, t114, &t115);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l47))[48] != 0);
                    if (t116) {
                        memmove(((uint8_t *)&F.l17), ((uint8_t *)&gm_l47), 48);
                    } else {
                        void * t117 = F.l0;
                        void * t118 = F.l1;
                        uint32_t t119 = F.l3;
                        gm_b0_1 t120 = gm_f_escape_pattern_6lpj7tc((void *)NULL, cx, NULL, t117, t118, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t121 = F.l0;
                    gm_t88rcucg1b7uk t122 = gm_l5;
                    uint32_t t123 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t121, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t123;
                    {
                        void * t124 = F.l0;
                        uint32_t t125 = gm_l18;
                        gm_ts6oqppjh0ba3 t126;
                        GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)13ULL));
                        gm_t7b7esi3n9r90 t127 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l15));
                        gm_t7b7esi3n9r90 t128 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t1ctgp8j1v2as t129;
                        GM_ST(uint16_t, (uint8_t *)&t129 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t129 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t125);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t129 + 4), t126);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 16), t127);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 64), t128);
                        void * t130 = F.l1;
                        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t130) + 120));
                        gm_t8658kcqovtpe t132;
                        memset(&t132, 0, sizeof t132);
                        GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t132 + 16), t129);
                        gm_b0_1 t133 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t124, &t132, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t134 = F.l0;
                        uint32_t t135 = gm_l18;
                        uint32_t t136 = F.l3;
                        gm_t7b7esi3n9r90 t137;
                        memset(&t137, 0, sizeof t137);
                        GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t137 + 16), t135);
                        gm_tejc6vv5sqno9 t138;
                        GM_ST(uint8_t, (uint8_t *)&t138 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t138 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t138 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t138 + 0), t137);
                        GM_ST(uint8_t, ((uint8_t *)&t138 + 48), ((uint8_t)1));
                        GM_ST(uint32_t, ((uint8_t *)&t138 + 52), t136);
                        void * t139 = F.l1;
                        gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t139) + 120));
                        gm_t8658kcqovtpe t141;
                        memset(&t141, 0, sizeof t141);
                        GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t141 + 16), t138);
                        gm_b0_1 t142 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t134, &t141, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t143 = F.l0;
                    gm_t88rcucg1b7uk t144 = gm_l5;
                    uint32_t t145 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t143, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t145;
                    {
                        void * t146 = F.l0;
                        uint32_t t147 = gm_l19;
                        uint8_t t148 = gm_l14;
                        if (t148) {
                            gm_ts6oqppjh0ba3 t149;
                            GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)11ULL));
                            gm_l48 = t149;
                        } else {
                            gm_ts6oqppjh0ba3 t150;
                            GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)10ULL));
                            gm_l48 = t150;
                        }
                        gm_ts6oqppjh0ba3 t151 = gm_l48;
                        gm_t7b7esi3n9r90 t152 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l15));
                        gm_t7b7esi3n9r90 t153 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l17));
                        gm_t1ctgp8j1v2as t154;
                        GM_ST(uint16_t, (uint8_t *)&t154 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t147);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t154 + 4), t151);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t154 + 16), t152);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t154 + 64), t153);
                        void * t155 = F.l1;
                        gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t155) + 120));
                        gm_t8658kcqovtpe t157;
                        memset(&t157, 0, sizeof t157);
                        GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t157 + 16), t154);
                        gm_b0_1 t158 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t146, &t157, t156);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t159 = F.l0;
                        uint32_t t160 = gm_l19;
                        uint32_t t161 = F.l3;
                        gm_t7b7esi3n9r90 t162;
                        memset(&t162, 0, sizeof t162);
                        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t162 + 16), t160);
                        gm_tejc6vv5sqno9 t163;
                        GM_ST(uint8_t, (uint8_t *)&t163 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t163 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t163 + 0), t162);
                        GM_ST(uint8_t, ((uint8_t *)&t163 + 48), ((uint8_t)1));
                        GM_ST(uint32_t, ((uint8_t *)&t163 + 52), t161);
                        void * t164 = F.l1;
                        gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t164) + 120));
                        gm_t8658kcqovtpe t166;
                        memset(&t166, 0, sizeof t166);
                        GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t166 + 16), t163);
                        gm_b0_1 t167 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t159, &t166, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)4ULL));
                    if (!t169) {
                        goto gm_lb6;
                    }
                    void * t170 = F.l0;
                    gm_t88rcucg1b7uk t171 = gm_l5;
                    uint32_t t172 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t172;
                    {
                        void * t173 = F.l0;
                        uint32_t t174 = gm_l20;
                        gm_tlg23kb3oj23s t175 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_t3349g713hmcr t176;
                        GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t176 + 65, 0);
                        GM_ST(uint16_t, (uint8_t *)&t176 + 66, 0);
                        GM_ST(uint32_t, (uint8_t *)&t176 + 68, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t176 + 0), t174);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t176 + 8), t175);
                        GM_ST(uint8_t, ((uint8_t *)&t176 + 64), ((uint8_t)0));
                        void * t177 = F.l1;
                        gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t177) + 120));
                        gm_t8658kcqovtpe t179;
                        memset(&t179, 0, sizeof t179);
                        GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)23ULL));
                        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t179 + 16), t176);
                        gm_b0_1 t180 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t173, &t179, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t181 = F.l0;
                        uint32_t t182 = gm_l20;
                        uint32_t t183 = F.l3;
                        gm_t7b7esi3n9r90 t184;
                        memset(&t184, 0, sizeof t184);
                        GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t184 + 16), t182);
                        gm_tejc6vv5sqno9 t185;
                        GM_ST(uint8_t, (uint8_t *)&t185 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t185 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t185 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t185 + 0), t184);
                        GM_ST(uint8_t, ((uint8_t *)&t185 + 48), ((uint8_t)0));
                        GM_ST(uint32_t, ((uint8_t *)&t185 + 52), t183);
                        void * t186 = F.l1;
                        gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t186) + 120));
                        gm_t8658kcqovtpe t188;
                        memset(&t188, 0, sizeof t188);
                        GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t188 + 16), t185);
                        gm_b0_1 t189 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t181, &t188, t187);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)5ULL));
                    if (!t191) {
                        goto gm_lb7;
                    }
                    void * t192 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                    gm_l21 = t192;
                    void * t193 = F.l0;
                    gm_t88rcucg1b7uk t194 = gm_l5;
                    uint32_t t195 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t193, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t195;
                    {
                        void * t196 = F.l0;
                        uint32_t t197 = gm_l22;
                        gm_tlg23kb3oj23s t198 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_t3349g713hmcr t199;
                        GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t199 + 65, 0);
                        GM_ST(uint16_t, (uint8_t *)&t199 + 66, 0);
                        GM_ST(uint32_t, (uint8_t *)&t199 + 68, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t199 + 0), t197);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t199 + 8), t198);
                        GM_ST(uint8_t, ((uint8_t *)&t199 + 64), ((uint8_t)0));
                        void * t200 = F.l1;
                        gm_tl874t2atnlh3 t201 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t200) + 120));
                        gm_t8658kcqovtpe t202;
                        memset(&t202, 0, sizeof t202);
                        GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)23ULL));
                        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t202 + 16), t199);
                        gm_b0_1 t203 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t196, &t202, t201);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t204 = F.l0;
                        uint32_t t205 = gm_l22;
                        uint32_t t206 = F.l3;
                        gm_t7b7esi3n9r90 t207;
                        memset(&t207, 0, sizeof t207);
                        GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t207 + 16), t205);
                        gm_tejc6vv5sqno9 t208;
                        GM_ST(uint8_t, (uint8_t *)&t208 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t208 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t208 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t208 + 0), t207);
                        GM_ST(uint8_t, ((uint8_t *)&t208 + 48), ((uint8_t)1));
                        GM_ST(uint32_t, ((uint8_t *)&t208 + 52), t206);
                        void * t209 = F.l1;
                        gm_tl874t2atnlh3 t210 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t209) + 120));
                        gm_t8658kcqovtpe t211;
                        memset(&t211, 0, sizeof t211);
                        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t211 + 16), t208);
                        gm_b0_1 t212 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t204, &t211, t210);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t213 = F.l0;
                        void * t214 = gm_l21;
                        void * t215 = gm_l21;
                        gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t215));
                        gm_tlcm4lvu3g1a8 t217;
                        GM_ST(uint32_t, (uint8_t *)&t217 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t217 + 0), ((uint64_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t217 + 8), t216);
                        gm_tfqmtuprvunod t218;
                        memset(&t218, 0, sizeof t218);
                        GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t218 + 16), t217);
                        gm_tlg23kb3oj23s t219;
                        gm_f_project_6hpd48e((void *)&t219, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), &t218);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t220 = F.l3;
                        gm_b0_1 t221 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t213, t214, &t219, t220);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    {
                        {
                            uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t223 = (uint8_t)(t222 == ((uint8_t)6ULL));
                            if (!t223) {
                                goto gm_lb10;
                            }
                            void * t224 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l23 = t224;
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)7ULL));
                            if (!t226) {
                                goto gm_lb11;
                            }
                            void * t227 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l23 = t227;
                            goto gm_lb9;
                        }
gm_lb11:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    void * t228 = F.l0;
                    gm_t88rcucg1b7uk t229 = gm_l5;
                    uint32_t t230 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t228, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t230;
                    {
                        void * t231 = F.l0;
                        uint32_t t232 = gm_l25;
                        gm_tlg23kb3oj23s t233 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_t3349g713hmcr t234;
                        GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t234 + 65, 0);
                        GM_ST(uint16_t, (uint8_t *)&t234 + 66, 0);
                        GM_ST(uint32_t, (uint8_t *)&t234 + 68, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t234 + 0), t232);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t234 + 8), t233);
                        GM_ST(uint8_t, ((uint8_t *)&t234 + 64), ((uint8_t)1));
                        void * t235 = F.l1;
                        gm_tl874t2atnlh3 t236 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t235) + 120));
                        gm_t8658kcqovtpe t237;
                        memset(&t237, 0, sizeof t237);
                        GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)23ULL));
                        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t237 + 16), t234);
                        gm_b0_1 t238 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t231, &t237, t236);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t239 = F.l0;
                        uint32_t t240 = gm_l25;
                        void * t241 = F.l1;
                        gm_t7b7esi3n9r90 t242;
                        memset(&t242, 0, sizeof t242);
                        GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t242 + 16), t240);
                        {
                            {
                                uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t241) + 8) + 0));
                                uint8_t t244 = (uint8_t)(t243 == ((uint8_t)6ULL));
                                if (!t244) {
                                    goto gm_lb13;
                                }
                                gm_l49 = ((uint8_t)1);
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                gm_l49 = ((uint8_t)0);
                                goto gm_lb12;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1065, 37);
                        }
gm_lb12:;
                        uint8_t t245 = gm_l49;
                        uint32_t t246 = F.l3;
                        gm_tejc6vv5sqno9 t247;
                        GM_ST(uint8_t, (uint8_t *)&t247 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t247 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t247 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t247 + 0), t242);
                        GM_ST(uint8_t, ((uint8_t *)&t247 + 48), t245);
                        GM_ST(uint32_t, ((uint8_t *)&t247 + 52), t246);
                        void * t248 = F.l1;
                        gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t248) + 120));
                        gm_t8658kcqovtpe t250;
                        memset(&t250, 0, sizeof t250);
                        GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t250 + 16), t247);
                        gm_b0_1 t251 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t239, &t250, t249);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t252 = F.l0;
                        void * t253 = gm_l23;
                        void * t254 = F.l0;
                        void * t255 = F.l1;
                        gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t255));
                        gm_tlg23kb3oj23s t257 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        uint64_t t258 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t254, t256);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t259 = gm_l23;
                        gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t259));
                        gm_tlcm4lvu3g1a8 t261;
                        GM_ST(uint32_t, (uint8_t *)&t261 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t261 + 0), t258);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t261 + 8), t260);
                        gm_tfqmtuprvunod t262;
                        memset(&t262, 0, sizeof t262);
                        GM_ST(uint8_t, ((uint8_t *)&t262 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t262 + 16), t261);
                        gm_tlg23kb3oj23s t263;
                        gm_f_project_6hpd48e((void *)&t263, (void *)NULL, cx, NULL, &t257, &t262);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t264 = F.l3;
                        gm_b0_1 t265 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t252, t253, &t263, t264);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)8ULL));
                    if (!t267) {
                        goto gm_lb15;
                    }
                    uint32_t t268 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l26 = t268;
                    void * t269 = GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8));
                    gm_l27 = t269;
                    void * t270 = F.l0;
                    gm_t88rcucg1b7uk t271 = gm_l5;
                    uint32_t t272 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t270, t271);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t272;
                    {
                        void * t273 = F.l0;
                        uint32_t t274 = gm_l28;
                        uint32_t t275 = gm_l26;
                        gm_tlg23kb3oj23s t276 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_t4j77n1ssj077 t277;
                        GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t277 + 68, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t277 + 0), t274);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t277 + 8), t276);
                        GM_ST(uint32_t, ((uint8_t *)&t277 + 64), t275);
                        void * t278 = F.l1;
                        gm_tl874t2atnlh3 t279 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t278) + 120));
                        gm_t8658kcqovtpe t280;
                        memset(&t280, 0, sizeof t280);
                        GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)24ULL));
                        GM_ST(gm_t4j77n1ssj077, ((uint8_t *)&t280 + 16), t277);
                        gm_b0_1 t281 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t273, &t280, t279);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t282 = F.l0;
                        uint32_t t283 = gm_l28;
                        uint32_t t284 = F.l3;
                        gm_t7b7esi3n9r90 t285;
                        memset(&t285, 0, sizeof t285);
                        GM_ST(uint8_t, ((uint8_t *)&t285 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t285 + 16), t283);
                        gm_tejc6vv5sqno9 t286;
                        GM_ST(uint8_t, (uint8_t *)&t286 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t286 + 50, 0);
                        GM_ST(uint64_t, (uint8_t *)&t286 + 56, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t286 + 0), t285);
                        GM_ST(uint8_t, ((uint8_t *)&t286 + 48), ((uint8_t)1));
                        GM_ST(uint32_t, ((uint8_t *)&t286 + 52), t284);
                        void * t287 = F.l1;
                        gm_tl874t2atnlh3 t288 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t287) + 120));
                        gm_t8658kcqovtpe t289;
                        memset(&t289, 0, sizeof t289);
                        GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t289 + 16), t286);
                        gm_b0_1 t290 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t282, &t289, t288);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t291 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l27)) != 0);
                            if (!t291) {
                                goto gm_lb17;
                            }
                            void * t292 = GM_LD(void *, ((uint8_t *)&gm_l27));
                            gm_l29 = t292;
                            {
                                void * t293 = F.l0;
                                void * t294 = gm_l29;
                                void * t295 = F.l0;
                                void * t296 = F.l1;
                                gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t296));
                                gm_tlg23kb3oj23s t298 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                                uint64_t t299 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t295, t297);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t300 = gm_l29;
                                gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t300));
                                gm_tlcm4lvu3g1a8 t302;
                                GM_ST(uint32_t, (uint8_t *)&t302 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t302 + 0), t299);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t302 + 8), t301);
                                gm_tfqmtuprvunod t303;
                                memset(&t303, 0, sizeof t303);
                                GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)4ULL));
                                GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t303 + 16), t302);
                                gm_tlg23kb3oj23s t304;
                                gm_f_project_6hpd48e((void *)&t304, (void *)NULL, cx, NULL, &t298, &t303);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint32_t t305 = F.l3;
                                gm_b0_1 t306 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t293, t294, &t304, t305);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                    }
gm_lb16:;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t307 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t308 = (uint8_t)(t307 == ((uint8_t)9ULL));
                    if (!t308) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    gm_l30 = t309;
                    void * t310 = F.l0;
                    void * t311 = F.l1;
                    gm_t88rcucg1b7uk t312 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t311));
                    uint8_t t313 = gm_f_is_bits_pbijkuf(t310, GM_CONV(gm_csguankko4lmhd, t312));
                    if (t313) {
                        {
                            void * t314 = F.l0;
                            void * t315 = F.l1;
                            gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t315));
                            void * t317 = F.l1;
                            gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t317) + 120));
                            gm_tlg23kb3oj23s t319 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                            gm_f_load_go4fgkp((void *)&F.l31, (void *)NULL, cx, NULL, t314, &t319, t316, t318);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        gm_b16_8 t320 = gm_l30;
                        gm_l32 = t320;
                        gm_l33 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t322 = gm_l33;
                                uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                uint8_t t324 = (t322 < t323);
                                if (!t324) {
                                    goto gm_lb19;
                                }
                                {
                                    void * t325 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                    uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                    uint64_t t327 = gm_l33;
                                    {
                                        {
                                            gm_ttggq7ng4kp3p t328 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t325) + (uintptr_t)t327 * 16));
                                            gm_l34 = t328;
                                            void * t329 = F.l0;
                                            void * t330 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                            gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t330));
                                            uint32_t t332 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t329, t331);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            gm_l35 = t332;
                                            {
                                                void * t333 = F.l0;
                                                uint32_t t334 = gm_l35;
                                                uint32_t t335 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                                gm_t7b7esi3n9r90 t336 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l31));
                                                gm_tcpisgbff1dqv t337;
                                                GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t337 + 8, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t337 + 68, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t337 + 72, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t337 + 0), t334);
                                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t337 + 16), t336);
                                                GM_ST(uint32_t, ((uint8_t *)&t337 + 64), t335);
                                                void * t338 = F.l1;
                                                gm_tl874t2atnlh3 t339 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t338) + 120));
                                                gm_t8658kcqovtpe t340;
                                                memset(&t340, 0, sizeof t340);
                                                GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)21ULL));
                                                GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t340 + 16), t337);
                                                gm_b0_1 t341 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t333, &t340, t339);
                                                if (gm_unw.kind) {
                                                    goto gm_ll321;
                                                }
                                            }
                                            void * t342 = F.l0;
                                            void * t343 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                            gm_t88rcucg1b7uk t344 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t343));
                                            uint32_t t345 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t342, t344);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            gm_l36 = t345;
                                            {
                                                void * t346 = F.l0;
                                                uint32_t t347 = gm_l36;
                                                gm_tlg23kb3oj23s t348;
                                                gm_f_local_place_qnf3mb2((void *)&t348, t347);
                                                uint32_t t349 = gm_l35;
                                                gm_t7b7esi3n9r90 t350;
                                                memset(&t350, 0, sizeof t350);
                                                GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)0ULL));
                                                GM_ST(uint32_t, ((uint8_t *)&t350 + 16), t349);
                                                gm_t6fp3q71vjrmk t351;
                                                GM_ST(uint64_t, (uint8_t *)&t351 + 56, 0);
                                                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t351 + 0), t348);
                                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t351 + 64), t350);
                                                void * t352 = F.l1;
                                                gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t352) + 120));
                                                gm_t8658kcqovtpe t354;
                                                memset(&t354, 0, sizeof t354);
                                                GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)25ULL));
                                                GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t354 + 16), t351);
                                                gm_b0_1 t355 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t346, &t354, t353);
                                                if (gm_unw.kind) {
                                                    goto gm_ll321;
                                                }
                                            }
                                            {
                                                void * t356 = F.l0;
                                                void * t357 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                                uint32_t t358 = gm_l36;
                                                gm_tlg23kb3oj23s t359;
                                                gm_f_local_place_qnf3mb2((void *)&t359, t358);
                                                uint32_t t360 = F.l3;
                                                gm_b0_1 t361 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t356, t357, &t359, t360);
                                                if (gm_unw.kind) {
                                                    goto gm_ll321;
                                                }
                                            }
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1078, 17);
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                uint64_t t362 = gm_l33;
                                uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                                uint64_t t365 = r363;
                                gm_l33 = t365;
                            }
                        }
                        goto gm_ls366;
gm_ll321:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb19; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb21; }
                        }
                        goto gm_lad;
gm_ls366:;
gm_lb19:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t367 = gm_l4;
                    void * t368 = F.l1;
                    gm_t88rcucg1b7uk t369 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t368));
                    gm_f_layout_a5ovnrm((void *)&gm_l50, (void *)NULL, cx, NULL, t367, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 16));
                    gm_l37 = t370;
                    gm_b16_8 t371 = gm_l30;
                    gm_l38 = t371;
                    gm_l39 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t373 = gm_l39;
                            uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                            uint8_t t375 = (t373 < t374);
                            if (!t375) {
                                goto gm_lb24;
                            }
                            {
                                void * t376 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                uint64_t t378 = gm_l39;
                                {
                                    {
                                        gm_ttggq7ng4kp3p t379 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t376) + (uintptr_t)t378 * 16));
                                        gm_l40 = t379;
                                        uint32_t t380 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                        uint64_t t381 = (uint64_t)t380;
                                        uint64_t t382 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                        uint8_t t383 = (t381 < t382);
                                        if (t383) {
                                            void * t384 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                            uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                            uint32_t t386 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                            uint64_t t387 = (uint64_t)t386;
                                            if ((uintptr_t)t387 >= (uintptr_t)(t385)) gm_trap_index((uintptr_t)t387, t385, gm_srccujoh2vgr9aq, 1089, 59);
                                            uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)t384) + (uintptr_t)t387 * 8));
                                            gm_l41 = t388;
                                        } else {
                                            gm_l41 = ((uint64_t)0ULL);
                                        }
                                        {
                                            void * t389 = F.l0;
                                            void * t390 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
                                            uint32_t t391 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                            uint64_t t392 = gm_l41;
                                            gm_tb31k5l794t7p t393;
                                            GM_ST(uint32_t, (uint8_t *)&t393 + 4, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t393 + 0), t391);
                                            GM_ST(uint64_t, ((uint8_t *)&t393 + 8), t392);
                                            gm_tfqmtuprvunod t394;
                                            memset(&t394, 0, sizeof t394);
                                            GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t394 + 16), t393);
                                            gm_tlg23kb3oj23s t395;
                                            gm_f_project_6hpd48e((void *)&t395, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), &t394);
                                            if (gm_unw.kind) {
                                                goto gm_ll372;
                                            }
                                            uint32_t t396 = F.l3;
                                            gm_b0_1 t397 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t389, t390, &t395, t396);
                                            if (gm_unw.kind) {
                                                goto gm_ll372;
                                            }
                                        }
                                        goto gm_lb27;
                                    }
gm_lb28:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1088, 13);
                                }
gm_lb27:;
                            }
gm_lb26:;
                            uint64_t t398 = gm_l39;
                            uint64_t r399; bool o400 = __builtin_add_overflow(t398, ((uint64_t)1ULL), &r399);
                            uint64_t t401 = r399;
                            gm_l39 = t401;
                        }
                    }
                    goto gm_ls402;
gm_ll372:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb26; }
                    }
                    goto gm_lad;
gm_ls402:;
gm_lb24:;
                    goto gm_lb1;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t403 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t404 = (uint8_t)(t403 == ((uint8_t)10ULL));
                    if (!t404) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t405 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    F.l42 = t405;
                    void * t406 = F.l0;
                    uint32_t t407 = gm_f_label_3373lil(t406);
                    F.l43 = t407;
                    void * t408 = F.l0;
                    gm_closure t409 = ((gm_closure){(void *)gm_f_fn_dduu5o8, (void *)&F.h, cx});
                    gm_b16_8 t410 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t408, t409);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t410;
                    {
                        void * t411 = F.l0;
                        uint32_t t412 = F.l43;
                        gm_b16_8 t413 = gm_l44;
                        gm_tiilmfd16v3fc t414;
                        GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t414 + 0), t412);
                        GM_ST(gm_b16_8, ((uint8_t *)&t414 + 8), t413);
                        void * t415 = F.l1;
                        gm_tl874t2atnlh3 t416 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t415) + 120));
                        gm_t8658kcqovtpe t417;
                        memset(&t417, 0, sizeof t417);
                        GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)27ULL));
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t417 + 16), t414);
                        gm_b0_1 t418 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t411, &t417, t416);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb29:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1020, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_off_o6q9s7n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_off_o6q9s7n F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_t53at80pfdus3 gm_l4;
    gm_tbccvikg6b96e gm_l5;
    uint64_t gm_l6;
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((uint8_t *)t5));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
    }
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)13ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1118, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l3;
    if (t11) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, ((uint8_t *)t12));
        gm_t88rcucg1b7uk t14 = gm_l1;
        gm_f_layout_a5ovnrm((void *)&gm_l5, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
    gm_l2 = t15;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t17 = (t16 > ((uint64_t)1ULL));
    if (t17) {
        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t18) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l6 = t20;
    } else {
        gm_l6 = ((uint64_t)0ULL);
    }
    uint64_t t21 = gm_l6;
    F.ret = t21;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_gn5u4tt(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_payload_ty_gn5u4tt F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    gm_t88rcucg1b7uk gm_l3; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_tkn42u7na3t81 gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t8, GM_CONV(gm_csguankko4lmhd, t9));
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)13ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t12;
            gm_t88rcucg1b7uk t13 = gm_l2;
            gm_tkn42u7na3t81 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[4] = 1;
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)14ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l3 = t17;
            gm_t88rcucg1b7uk t18 = gm_l3;
            gm_tkn42u7na3t81 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[4] = 1;
            gm_l4 = t19;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tkn42u7na3t81 t20;
            memset(&t20, 0, sizeof t20);
            gm_l4 = t20;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1153, 44);
    }
gm_lb1:;
    gm_tkn42u7na3t81 t21 = gm_l4;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_place_oohr45n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_place_oohr45n F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* s */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* l */
    uint64_t gm_l6; /* decl */
    gm_b16_8 gm_l7; /* name */
    gm_tl874t2atnlh3 gm_l8; /* span */
    void * gm_l9; /* x */
    void * gm_l11; /* base */
    uint32_t gm_l12; /* index */
    gm_b16_8 gm_l14; /* offs */
    uint64_t gm_l15; /* off */
    void * gm_l16; /* base */
    void * gm_l17; /* index */
    uint8_t gm_l18; /* checked */
    GM_STACK_CHECK(&F, 1344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tfonjjajjj3jq gm_l19;
    gm_tbccvikg6b96e gm_l20;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    gm_l2 = t12;
    void * t13 = gm_l1;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)15ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t13) + 8) + 8));
            gm_l3 = t16;
            {
                uint32_t t17 = gm_l3;
                gm_tlg23kb3oj23s t18;
                gm_f_local_place_qnf3mb2((void *)&t18, t17);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t18);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)16ULL));
            if (!t20) {
                goto gm_lb3;
            }
            uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)t13) + 8) + 8));
            gm_l4 = t21;
            uint32_t t22 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 4));
            gm_l5 = t22;
            {
                uint32_t t23 = gm_l4;
                uint32_t t24 = gm_l5;
                gm_tcc1rq4n1avb2 t25;
                GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t23);
                GM_ST(uint32_t, ((uint8_t *)&t25 + 4), t24);
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
                gm_tu78tssgij60g t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t27 + 8), t25);
                gm_tlg23kb3oj23s t28;
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t28 + 0), t27);
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 40), t26);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t28);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)17ULL));
            if (!t30) {
                goto gm_lb4;
            }
            uint64_t t31 = GM_LD(uint64_t, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l6 = t31;
            {
                uint64_t t32 = gm_l6;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
                gm_tu78tssgij60g t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t34 + 8), t32);
                gm_tlg23kb3oj23s t35;
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t35 + 0), t34);
                GM_ST(gm_b16_8, ((uint8_t *)&t35 + 40), t33);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t35);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)14ULL));
            if (!t37) {
                goto gm_lb5;
            }
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l7 = t38;
            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l8 = t39;
            {
                gm_b16_8 t40 = gm_l7;
                gm_tl874t2atnlh3 t41 = gm_l8;
                void * t42 = gm_l1;
                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t42));
                gm_tpfol4ocdvoij t44;
                GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t40);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t44 + 16), t41);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 28), t43);
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
                gm_tu78tssgij60g t46;
                GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)3ULL));
                GM_ST(gm_tpfol4ocdvoij, ((uint8_t *)&t46 + 8), t44);
                gm_tlg23kb3oj23s t47;
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t47 + 0), t46);
                GM_ST(gm_b16_8, ((uint8_t *)&t47 + 40), t45);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t47);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb5:;
        {
            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)18ULL));
            if (!t49) {
                goto gm_lb6;
            }
            void * t50 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l9 = t50;
            {
                void * t51 = gm_l0;
                void * t52 = gm_l9;
                gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t53 = gm_l0;
                void * t54 = gm_l9;
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
                void * t56 = gm_l9;
                gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
                uint32_t t58 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t53, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l10)), t55, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
                gm_tu78tssgij60g t60;
                memset(&t60, 0, sizeof t60);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)4ULL));
                GM_ST(uint32_t, ((uint8_t *)&t60 + 8), t58);
                gm_tlg23kb3oj23s t61;
                GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t61 + 0), t60);
                GM_ST(gm_b16_8, ((uint8_t *)&t61 + 40), t59);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t61);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb6:;
        {
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)19ULL));
            if (!t63) {
                goto gm_lb7;
            }
            void * t64 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l11 = t64;
            uint32_t t65 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l12 = t65;
            void * t66 = gm_l0;
            void * t67 = gm_l11;
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t67));
            uint8_t t69 = gm_f_is_bits_pbijkuf(t66, GM_CONV(gm_csguankko4lmhd, t68));
            if (t69) {
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
            {
                void * t70 = gm_l0;
                void * t71 = gm_l11;
                gm_f_operand_place_cdle4ls((void *)&gm_l19, (void *)NULL, cx, NULL, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l19))[56] != 0);
            if (t72) {
                memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l19), 56);
            } else {
                memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
                goto gm_fdone;
            }
            {
                void * t73 = gm_l2;
                void * t74 = gm_l11;
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t74));
                gm_f_layout_a5ovnrm((void *)&gm_l20, (void *)NULL, cx, NULL, t73, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 16));
            gm_l14 = t76;
            uint32_t t77 = gm_l12;
            uint64_t t78 = (uint64_t)t77;
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
            uint8_t t80 = (t78 < t79);
            if (t80) {
                void * t81 = GM_LD(void *, ((uint8_t *)&gm_l14));
                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint32_t t83 = gm_l12;
                uint64_t t84 = (uint64_t)t83;
                if ((uintptr_t)t84 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t84, t82, gm_srccujoh2vgr9aq, 567, 53);
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t81) + (uintptr_t)t84 * 8));
                gm_l15 = t85;
            } else {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                uint32_t t86 = gm_l12;
                uint64_t t87 = gm_l15;
                gm_tb31k5l794t7p t88;
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t88 + 0), t86);
                GM_ST(uint64_t, ((uint8_t *)&t88 + 8), t87);
                gm_tfqmtuprvunod t89;
                memset(&t89, 0, sizeof t89);
                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t89 + 16), t88);
                gm_tlg23kb3oj23s t90;
                gm_f_project_6hpd48e((void *)&t90, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out) + 60, 0);
                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out)), t90);
                ((uint8_t *)&(*(gm_tfonjjajjj3jq *)gm_out))[56] = 1;
                goto gm_fdone;
            }
        }
gm_lb7:;
        {
            uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)20ULL));
            if (!t92) {
                goto gm_lb8;
            }
            void * t93 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l16 = t93;
            void * t94 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l17 = t94;
            uint8_t t95 = GM_LD(uint8_t, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l18 = t95;
            void * t96 = gm_l0;
            void * t97 = gm_l1;
            void * t98 = gm_l16;
            void * t99 = gm_l17;
            uint8_t t100 = gm_l18;
            gm_f_index_place_3gfu5dn((void *)&(*(gm_tfonjjajjj3jq *)gm_out), (void *)NULL, cx, NULL, t96, t97, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb8:;
        {
            memset(&(*(gm_tfonjjajjj3jq *)gm_out), 0, sizeof (*(gm_tfonjjajjj3jq *)gm_out));
            goto gm_fdone;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 554, 5);
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
inline __attribute__((aligned(4))) void gm_f_project_6hpd48e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_project_6hpd48e F;
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_hft9rao((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 40));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_vbdqifj(t3, t5);
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 103, 5);
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_tftb1odc739t5 t9 = gm_f_push_kki5o6e(t8, &(*(gm_tfqmtuprvunod *)((uint8_t *)&F.l1)));
        gm_l4 = t9;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srccujoh2vgr9aq, 104, 5);
    {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_tu78tssgij60g t12 = GM_LD(gm_tu78tssgij60g, ((uint8_t *)&F.l0));
        GM_ST(gm_tu78tssgij60g, ((uint8_t *)&(*(gm_tlg23kb3oj23s *)gm_out) + 0), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tlg23kb3oj23s *)gm_out) + 40), t11);
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
extern __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_for_6fafafc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_result_for_6fafafc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    gm_tbl8il92l97ut gm_l2; /* h */
    GM_STACK_CHECK(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l3;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = (uint8_t)(((((uint8_t *)t6) + 240))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_tbl8il92l97ut t8 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)t6) + 240));
            gm_l2 = t8;
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l2));
            void * t10 = gm_l1;
            uint8_t t11 = ((t9 == t10));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l0;
            gm_t5lf9i3n1r3co t13;
            memset(&t13, 0, sizeof t13);
            GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t12) + 240), t13);
            {
                {
                    {
                        void * t14 = gm_l0;
                        void * t15 = GM_LD(void *, ((uint8_t *)t14));
                        void * t16 = gm_l1;
                        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t16));
                        gm_f_layout_a5ovnrm((void *)&gm_l3, (void *)NULL, cx, NULL, t15, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
                    uint8_t t19 = (t18 > ((uint64_t)0ULL));
                    if (!t19) {
                        goto gm_lb4;
                    }
                    {
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, ((uint8_t *)t20));
                        void * t22 = (void *)(((uint8_t *)t21) + 8);
                        void * t23 = gm_l1;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
                        gm_t53at80pfdus3 t25;
                        gm_f_kind_jcnt4hm((void *)&t25, t22, GM_CONV(gm_csguankko4lmhd, t24));
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
                        uint8_t t27 = (uint8_t)(!(t26 == ((uint8_t)5ULL)));
                        if (!t27) {
                            goto gm_lb4;
                        }
                    }
                    void * t28 = gm_l0;
                    void * t29 = GM_LD(void *, (((uint8_t *)t28) + 16));
                    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 16) + 8));
                    uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t32 = (uint64_t)t31;
                    if ((uintptr_t)t32 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t32, t30, gm_srccujoh2vgr9aq, 870, 81);
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + (uintptr_t)t32 * 32));
                    void * t34 = gm_l1;
                    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t34));
                    uint8_t t36 = (gm_eq9m6mvtmgde(((uint8_t *)&t33), ((uint8_t *)&t35)));
                    if (!t36) {
                        goto gm_lb4;
                    }
                    void * t37 = gm_l0;
                    GM_ST(uint8_t, (((uint8_t *)t37) + 264), ((uint8_t)1));
                    uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
                    gm_t9805qtonbsag t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t39), t38);
                    ((uint8_t *)&t39)[4] = 1;
                    F.ret = t39;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t40 = gm_l0;
    void * t41 = gm_l1;
    gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t41));
    gm_t9805qtonbsag t43 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t40, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_local_pp1s755(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_result_local_pp1s755 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tbccvikg6b96e gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l3, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_t53at80pfdus3 t11;
        gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        uint8_t t13 = (uint8_t)((t12 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)5ULL));
        gm_l2 = t13;
    }
    uint8_t t15 = gm_l2;
    if (t15) {
        gm_t9805qtonbsag t16;
        memset(&t16, 0, sizeof t16);
        F.ret = t16;
        goto gm_fret;
    }
    void * t17 = gm_l0;
    gm_t88rcucg1b7uk t18 = gm_l1;
    uint32_t t19 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9805qtonbsag t20;
    GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t20), t19);
    ((uint8_t *)&t20)[4] = 1;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_slice_dup5j25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_slice_dup5j25 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    gm_tao63pk3r18v5 gm_l2; /* sl */
    void * gm_l3; /* s */
    gm_t88rcucg1b7uk gm_l4; /* usize_ty */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    uint64_t gm_l6; /* esz */
    uint64_t gm_l9; /* n */
    uint32_t gm_l11; /* d */
    uint8_t gm_l12; /* is_const */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    uint64_t gm_l15; /* n */
    void * gm_l17; /* x */
    uint8_t gm_l19; /* inclusive */
    void * gm_l20; /* x */
    uint32_t gm_l21; /* d */
    GM_STACK_CHECK(&F, 3565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l22;
    gm_tbccvikg6b96e gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_tfonjjajjj3jq gm_l25;
    gm_tfonjjajjj3jq gm_l26;
    uint8_t gm_l27;
    gm_t53at80pfdus3 gm_l28;
    gm_t7b7esi3n9r90 gm_l29;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        gm_l3 = t20;
        void * t21 = gm_l3;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
        {
            {
                {
                    {
                        void * t24 = gm_l3;
                        void * t25 = (void *)(((uint8_t *)t24) + 8);
                        void * t26 = gm_l1;
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
                        gm_f_kind_jcnt4hm((void *)&gm_l22, t25, GM_CONV(gm_csguankko4lmhd, t27));
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                        uint8_t t29 = (uint8_t)(t28 == ((uint8_t)10ULL));
                        if (!t29) {
                            goto gm_lb2;
                        }
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                        gm_l5 = t30;
                        void * t31 = gm_l3;
                        gm_t88rcucg1b7uk t32 = gm_l5;
                        gm_f_layout_a5ovnrm((void *)&gm_l23, (void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
                        gm_l6 = t33;
                        gm_t88rcucg1b7uk t34 = gm_l4;
                        memset(&F.l7, 0, sizeof F.l7);
                        GM_ST(uint8_t, ((uint8_t *)&F.l7 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l7 + 16), t34);
                        memset(&F.l8, 0, sizeof F.l8);
                        void * t35 = gm_l3;
                        void * t36 = (void *)(((uint8_t *)t35) + 8);
                        void * t37 = GM_LD(void *, ((uint8_t *)a2));
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t37));
                        gm_f_kind_jcnt4hm((void *)&gm_l24, t36, GM_CONV(gm_csguankko4lmhd, t38));
                        {
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)11ULL));
                                if (!t40) {
                                    goto gm_lb4;
                                }
                                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                gm_l9 = t41;
                                void * t42 = gm_l3;
                                void * t43 = GM_LD(void *, ((uint8_t *)a2));
                                uint8_t t44 = gm_f_is_place_expr_6llket3(t42, t43);
                                uint8_t t45 = (uint8_t)!t44;
                                if (t45) {
                                    void * t46 = gm_l0;
                                    void * t47 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t46, t47);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                {
                                    void * t48 = gm_l0;
                                    void * t49 = GM_LD(void *, ((uint8_t *)a2));
                                    gm_f_place_oohr45n((void *)&gm_l25, (void *)NULL, cx, NULL, t48, t49);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l25))[56] != 0);
                                if (t50) {
                                    memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l25), 56);
                                } else {
                                    void * t51 = gm_l0;
                                    void * t52 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t51, t52);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                void * t53 = gm_l0;
                                void * t54 = (void *)((uint8_t *)&F.l10);
                                gm_f_mark_addressed_t8qojin(t53, t54);
                                {
                                    void * t55 = gm_l0;
                                    void * t56 = gm_l3;
                                    void * t57 = (void *)(((uint8_t *)t56) + 8);
                                    gm_t88rcucg1b7uk t58 = gm_l5;
                                    gm_tbngattl1fo51 t59;
                                    GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59 + 4), t58);
                                    gm_t53at80pfdus3 t60;
                                    memset(&t60, 0, sizeof t60);
                                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)9ULL));
                                    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t60 + 8), t59);
                                    gm_t88rcucg1b7uk t61 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t57, &t60);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint32_t t62 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t55, t61);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l11 = t62;
                                }
                                {
                                    void * t63 = gm_l0;
                                    uint32_t t64 = gm_l11;
                                    gm_tlg23kb3oj23s t65 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l10));
                                    gm_tlpcrb3vu6o2d t66;
                                    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t64);
                                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t66 + 8), t65);
                                    void * t67 = gm_l1;
                                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                                    gm_t8658kcqovtpe t69;
                                    memset(&t69, 0, sizeof t69);
                                    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t69 + 16), t66);
                                    gm_b0_1 t70 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t63, &t69, t68);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    uint32_t t71 = gm_l11;
                                    memset(&F.l7, 0, sizeof F.l7);
                                    GM_ST(uint8_t, ((uint8_t *)&F.l7 + 0), ((uint8_t)0ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&F.l7 + 16), t71);
                                }
                                {
                                    gm_t88rcucg1b7uk t72 = gm_l4;
                                    uint64_t t73 = gm_l9;
                                    gm_tfbnciphf3261 t74;
                                    GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t74 + 0), t72);
                                    GM_ST(uint64_t, ((uint8_t *)&t74 + 8), t73);
                                    gm_t7b7esi3n9r90 t75;
                                    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t75 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)1ULL));
                                    GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t75 + 16), t74);
                                    GM_ST(uint8_t, (uint8_t *)&F.l8 + 49, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.l8 + 50, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.l8 + 52, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l8 + 56, 0);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l8), t75);
                                    ((uint8_t *)&F.l8)[48] = 1;
                                }
                                goto gm_lb3;
                            }
gm_lb4:;
                            {
                                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t77 = (uint8_t)(t76 == ((uint8_t)10ULL));
                                if (!t77) {
                                    goto gm_lb5;
                                }
                                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 8));
                                gm_l12 = t78;
                                {
                                    void * t79 = gm_l0;
                                    void * t80 = GM_LD(void *, ((uint8_t *)a2));
                                    gm_f_operand_place_cdle4ls((void *)&gm_l26, (void *)NULL, cx, NULL, t79, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l26))[56] != 0);
                                if (t81) {
                                    memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l26), 56);
                                } else {
                                    void * t82 = gm_l0;
                                    void * t83 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t82, t83);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                {
                                    void * t84 = gm_l0;
                                    gm_tb31k5l794t7p t85;
                                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t85 + 0), ((uint32_t)0ULL));
                                    GM_ST(uint64_t, ((uint8_t *)&t85 + 8), ((uint64_t)0ULL));
                                    gm_tfqmtuprvunod t86;
                                    memset(&t86, 0, sizeof t86);
                                    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t86 + 16), t85);
                                    gm_tlg23kb3oj23s t87;
                                    gm_f_project_6hpd48e((void *)&t87, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t86);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t88 = gm_l3;
                                    void * t89 = (void *)(((uint8_t *)t88) + 8);
                                    uint8_t t90 = gm_l12;
                                    gm_t88rcucg1b7uk t91 = gm_l5;
                                    gm_tbngattl1fo51 t92;
                                    GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), t90);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t92 + 4), t91);
                                    gm_t53at80pfdus3 t93;
                                    memset(&t93, 0, sizeof t93);
                                    GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)9ULL));
                                    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t93 + 8), t92);
                                    gm_t88rcucg1b7uk t94 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t89, &t93);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t95 = gm_l1;
                                    gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 72));
                                    gm_f_load_go4fgkp((void *)&F.l7, (void *)NULL, cx, NULL, t84, &t87, t94, t96);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    void * t97 = gm_l0;
                                    void * t98 = gm_l0;
                                    gm_tlg23kb3oj23s t99 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l13));
                                    uint64_t t100 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t98);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tb31k5l794t7p t101;
                                    GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t101 + 0), ((uint32_t)1ULL));
                                    GM_ST(uint64_t, ((uint8_t *)&t101 + 8), t100);
                                    gm_tfqmtuprvunod t102;
                                    memset(&t102, 0, sizeof t102);
                                    GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t102 + 16), t101);
                                    gm_tlg23kb3oj23s t103;
                                    gm_f_project_6hpd48e((void *)&t103, (void *)NULL, cx, NULL, &t99, &t102);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t88rcucg1b7uk t104 = gm_l4;
                                    void * t105 = gm_l1;
                                    gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 72));
                                    gm_t7b7esi3n9r90 t107;
                                    gm_f_load_go4fgkp((void *)&t107, (void *)NULL, cx, NULL, t97, &t103, t104, t106);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&F.l8 + 49, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.l8 + 50, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.l8 + 52, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l8 + 56, 0);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l8), t107);
                                    ((uint8_t *)&F.l8)[48] = 1;
                                }
                                goto gm_lb3;
                            }
gm_lb5:;
                            {
                                uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t109 = (uint8_t)(t108 == ((uint8_t)9ULL));
                                if (!t109) {
                                    goto gm_lb6;
                                }
                                {
                                    {
                                        uint8_t t110 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 16)) != 0);
                                        if (t110) {
                                            goto gm_lb8;
                                        }
                                        gm_l27 = ((uint8_t)1);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    {
                                        gm_l27 = ((uint8_t)0);
                                        goto gm_lb7;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1312, 16);
                                }
gm_lb7:;
                                uint8_t t111 = gm_l27;
                                if (t111) {
                                    void * t112 = gm_l0;
                                    void * t113 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t112, t113);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                void * t114 = gm_l0;
                                void * t115 = GM_LD(void *, ((uint8_t *)a2));
                                gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t114, t115);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb3;
                            }
gm_lb6:;
                            {
                                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)8ULL));
                                if (!t117) {
                                    goto gm_lb10;
                                }
                                gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l24) + 8) + 4));
                                gm_l14 = t118;
                                {
                                    {
                                        {
                                            void * t119 = gm_l3;
                                            void * t120 = (void *)(((uint8_t *)t119) + 8);
                                            gm_t88rcucg1b7uk t121 = gm_l14;
                                            gm_f_kind_jcnt4hm((void *)&gm_l28, t120, GM_CONV(gm_csguankko4lmhd, t121));
                                        }
                                        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                                        uint8_t t123 = (uint8_t)(t122 == ((uint8_t)11ULL));
                                        if (!t123) {
                                            goto gm_lb12;
                                        }
                                        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                        gm_l15 = t124;
                                        {
                                            void * t125 = gm_l0;
                                            void * t126 = GM_LD(void *, ((uint8_t *)a2));
                                            gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t125, t126);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        {
                                            gm_t88rcucg1b7uk t127 = gm_l4;
                                            uint64_t t128 = gm_l15;
                                            gm_tfbnciphf3261 t129;
                                            GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t129 + 0), t127);
                                            GM_ST(uint64_t, ((uint8_t *)&t129 + 8), t128);
                                            gm_t7b7esi3n9r90 t130;
                                            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t130 + 8, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t130 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t130 + 40, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t130 + 16), t129);
                                            GM_ST(uint8_t, (uint8_t *)&F.l8 + 49, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l8 + 50, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l8 + 52, 0);
                                            GM_ST(uint64_t, (uint8_t *)&F.l8 + 56, 0);
                                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l8), t130);
                                            ((uint8_t *)&F.l8)[48] = 1;
                                        }
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    void * t131 = gm_l0;
                                    void * t132 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t131, t132);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb11:;
                                goto gm_lb3;
                            }
gm_lb10:;
                            {
                                void * t133 = gm_l0;
                                void * t134 = gm_l1;
                                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t133, t134);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1296, 5);
                        }
gm_lb3:;
                    }
                    {
                        gm_t88rcucg1b7uk t135 = gm_l4;
                        gm_tfbnciphf3261 t136;
                        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136 + 0), t135);
                        GM_ST(uint64_t, ((uint8_t *)&t136 + 8), ((uint64_t)0ULL));
                        GM_ST(uint8_t, (uint8_t *)&F.l16 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l16 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l16 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l16 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l16 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l16 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l16 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&F.l16 + 16), t136);
                        {
                            {
                                uint8_t t137 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 8)) != 0);
                                if (!t137) {
                                    goto gm_lb15;
                                }
                                void * t138 = GM_LD(void *, (((uint8_t *)a2) + 8));
                                gm_l17 = t138;
                                void * t139 = gm_l0;
                                void * t140 = gm_l17;
                                gm_f_expr_tp86bm9((void *)&F.l16, (void *)NULL, cx, NULL, t139, t140);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                        }
gm_lb14:;
                        gm_t88rcucg1b7uk t141 = gm_l4;
                        gm_tfbnciphf3261 t142;
                        GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t142 + 0), t141);
                        GM_ST(uint64_t, ((uint8_t *)&t142 + 8), ((uint64_t)0ULL));
                        GM_ST(uint8_t, (uint8_t *)&F.l18 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l18 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l18 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l18 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l18 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l18 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&F.l18 + 16), t142);
                        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)a2) + 24));
                        gm_l19 = t143;
                        {
                            {
                                uint8_t t144 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 16)) != 0);
                                if (!t144) {
                                    goto gm_lb17;
                                }
                                void * t145 = GM_LD(void *, (((uint8_t *)a2) + 16));
                                gm_l20 = t145;
                                void * t146 = gm_l0;
                                void * t147 = gm_l20;
                                gm_f_expr_tp86bm9((void *)&F.l18, (void *)NULL, cx, NULL, t146, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb16;
                            }
gm_lb17:;
                            uint8_t t148 = (uint8_t)((((uint8_t *)&F.l8))[48] != 0);
                            if (t148) {
                                memmove(((uint8_t *)&gm_l29), ((uint8_t *)&F.l8), 48);
                            } else {
                                void * t149 = gm_l0;
                                void * t150 = gm_l1;
                                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t149, t150);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l29), 48);
                            gm_l19 = ((uint8_t)0);
                        }
gm_lb16:;
                        void * t151 = gm_l0;
                        void * t152 = gm_l0;
                        uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)t152) + 232));
                        uint32_t r154; bool o155 = __builtin_add_overflow(t153, ((uint32_t)1ULL), &r154);
                        if (o155) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 1332, 5);
                        uint32_t t156 = r154;
                        GM_ST(uint32_t, (((uint8_t *)t151) + 232), t156);
                        void * t157 = gm_l0;
                        uint8_t t158 = gm_l19;
                        gm_t7b7esi3n9r90 t159 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t7b7esi3n9r90 t160 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l18));
                        gm_tf8kfhcnval78 t161 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l8));
                        gm_thvv7nb91atip t162;
                        GM_ST(uint8_t, (uint8_t *)&t162 + 161, 0);
                        GM_ST(uint16_t, (uint8_t *)&t162 + 162, 0);
                        GM_ST(uint32_t, (uint8_t *)&t162 + 164, 0);
                        GM_ST(uint64_t, (uint8_t *)&t162 + 168, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t162 + 0), t159);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t162 + 48), t160);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t162 + 96), t161);
                        GM_ST(uint8_t, ((uint8_t *)&t162 + 160), t158);
                        gm_tkj0hg83g38nr t163;
                        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_thvv7nb91atip, ((uint8_t *)&t163 + 16), t162);
                        void * t164 = gm_l1;
                        gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t164) + 72));
                        gm_t8658kcqovtpe t166;
                        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t166 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)26ULL));
                        GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t166 + 16), t163);
                        gm_b0_1 t167 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t157, &t166, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t168 = gm_l0;
                        void * t169 = gm_l1;
                        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t169));
                        uint32_t t171 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t168, t170);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l21 = t171;
                    }
                    {
                        void * t172 = gm_l0;
                        uint32_t t173 = gm_l21;
                        uint8_t t174 = gm_l19;
                        uint64_t t175 = gm_l6;
                        gm_t7b7esi3n9r90 t176 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                        gm_t7b7esi3n9r90 t177 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t7b7esi3n9r90 t178 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l18));
                        gm_tgroihbjsmdvm t179;
                        GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t179 + 8, 0);
                        GM_ST(uint8_t, (uint8_t *)&t179 + 161, 0);
                        GM_ST(uint16_t, (uint8_t *)&t179 + 162, 0);
                        GM_ST(uint32_t, (uint8_t *)&t179 + 164, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t179 + 0), t173);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t179 + 16), t176);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t179 + 64), t177);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t179 + 112), t178);
                        GM_ST(uint8_t, ((uint8_t *)&t179 + 160), t174);
                        GM_ST(uint64_t, ((uint8_t *)&t179 + 168), t175);
                        void * t180 = gm_l1;
                        gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t180) + 72));
                        gm_t8658kcqovtpe t182;
                        GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t182 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t182 + 192, 0);
                        GM_ST(uint64_t, (uint8_t *)&t182 + 200, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)12ULL));
                        GM_ST(gm_tgroihbjsmdvm, ((uint8_t *)&t182 + 16), t179);
                        gm_b0_1 t183 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t172, &t182, t181);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t184 = gm_l21;
                        memset(&(*(gm_t7b7esi3n9r90 *)gm_out), 0, sizeof (*(gm_t7b7esi3n9r90 *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&(*(gm_t7b7esi3n9r90 *)gm_out) + 16), t184);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                void * t185 = gm_l0;
                void * t186 = gm_l1;
                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t185, t186);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_k90qtjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_k90qtjr F;
    void * gm_l1; /* st */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* init */
    void * gm_l4; /* x */
    uint8_t gm_l6; /* taken */
    void * gm_l7; /* last */
    uint32_t gm_l8; /* d */
    gm_b16_8 gm_l10; /* locals */
    void * gm_l11; /* init */
    gm_b16_8 gm_l13; /* offs */
    gm_b16_8 gm_l14; /* elems */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_t9805qtonbsag gm_l18; /* x */
    uint32_t gm_l19; /* l */
    void * gm_l21; /* place */
    void * gm_l22; /* value */
    void * gm_l24; /* root */
    void * gm_l25; /* base */
    uint64_t gm_l29; /* k */
    uint32_t gm_l30; /* index */
    uint32_t gm_l31; /* d */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* j */
    void * gm_l36; /* base */
    uint32_t gm_l37; /* index */
    uint32_t gm_l38; /* d */
    gm_b16_8 gm_l41; /* places */
    void * gm_l42; /* value */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    void * gm_l46; /* x */
    void * gm_l47; /* q */
    uint32_t gm_l49; /* l */
    gm_b16_8 gm_l50; /* offs */
    gm_b16_8 gm_l51; /* elems */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* i */
    void * gm_l58; /* x */
    uint8_t gm_l59; /* is_err */
    gm_b16_8 gm_l61; /* insts */
    GM_STACK_CHECK(&F, 8349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l62;
    gm_tfonjjajjj3jq gm_l63;
    gm_tbccvikg6b96e gm_l64;
    gm_t53at80pfdus3 gm_l65;
    gm_tp0pp2cmr6tvd gm_l66;
    gm_tbccvikg6b96e gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tfonjjajjj3jq gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tfonjjajjj3jq gm_l72;
    uint8_t gm_l73;
    gm_tbccvikg6b96e gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tlg23kb3oj23s gm_l76;
    gm_tfonjjajjj3jq gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tbccvikg6b96e gm_l79;
    gm_t53at80pfdus3 gm_l80;
    gm_t30kh7ksmrtsb gm_l81;
    gm_tbccvikg6b96e gm_l82;
    {
        void * t49 = gm_l1;
        {
            {
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
                    if (!t51) {
                        goto gm_lb2;
                    }
                    uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t49) + 8));
                    gm_l2 = t52;
                    void * t53 = GM_LD(void *, ((((uint8_t *)t49) + 8) + 8));
                    gm_l3 = t53;
                    uint8_t t54 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                    if (t54) {
                        void * t55 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        gm_l4 = t55;
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t56 = F.l0;
                        void * t57 = gm_l4;
                        uint32_t t58 = gm_l2;
                        gm_tbl8il92l97ut t59;
                        GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t59 + 0), t57);
                        GM_ST(uint32_t, ((uint8_t *)&t59 + 8), t58);
                        gm_t5lf9i3n1r3co t60;
                        GM_ST(uint8_t, (uint8_t *)&t60 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t60 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t60), t59);
                        ((uint8_t *)&t60)[16] = 1;
                        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t56) + 240), t60);
                    }
                    void * t61 = F.l0;
                    GM_ST(uint8_t, (((uint8_t *)t61) + 264), ((uint8_t)0));
                    {
                        void * t62 = F.l0;
                        void * t63 = gm_l4;
                        gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t62, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t64 = F.l0;
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t64) + 264));
                    gm_l6 = t65;
                    {
                        void * t66 = F.l0;
                        gm_t5lf9i3n1r3co t67;
                        memset(&t67, 0, sizeof t67);
                        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t66) + 240), t67);
                    }
                    void * t68 = F.l0;
                    GM_ST(uint8_t, (((uint8_t *)t68) + 264), ((uint8_t)0));
                    void * t69 = F.l0;
                    void * t70 = gm_l4;
                    uint8_t t71 = gm_f_diverges_p3okmol(t69, t70);
                    if (t71) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t72 = F.l0;
                        void * t73 = GM_LD(void *, ((uint8_t *)t72));
                        void * t74 = gm_l4;
                        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t74));
                        gm_f_layout_a5ovnrm((void *)&gm_l62, (void *)NULL, cx, NULL, t73, t75);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t76 = GM_LD(uint64_t, ((uint8_t *)&gm_l62));
                    uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
                    if (t77) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        {
                            uint8_t t78 = gm_l6;
                            if (!t78) {
                                goto gm_lb4;
                            }
                            void * t79 = F.l0;
                            uint64_t t80 = GM_LD(uint64_t, ((((uint8_t *)t79) + 96) + 8));
                            uint8_t t81 = (t80 > ((uint64_t)0ULL));
                            if (!t81) {
                                goto gm_lb4;
                            }
                            void * t82 = F.l0;
                            void * t83 = GM_LD(void *, (((uint8_t *)t82) + 96));
                            uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 96) + 8));
                            void * t85 = F.l0;
                            uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)t85) + 96) + 8));
                            uint64_t r87; bool o88 = __builtin_sub_overflow(t86, ((uint64_t)1ULL), &r87);
                            if (o88) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 741, 39);
                            uint64_t t89 = r87;
                            if ((uintptr_t)t89 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t89, t84, gm_srccujoh2vgr9aq, 741, 26);
                            void * t90 = (void *)(((uint8_t *)t83) + (uintptr_t)t89 * 224);
                            gm_l7 = t90;
                            {
                                {
                                    {
                                        void * t91 = gm_l7;
                                        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)t91) + 0));
                                        uint8_t t93 = (uint8_t)(t92 == ((uint8_t)1ULL));
                                        if (!t93) {
                                            goto gm_lb6;
                                        }
                                        uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)t91) + 16));
                                        gm_l8 = t94;
                                        memmove(((uint8_t *)&F.l9), ((((uint8_t *)t91) + 16) + 8), 56);
                                    }
                                    {
                                        uint32_t t95 = gm_l8;
                                        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t97 = (uint8_t)(t96 == ((uint8_t)0ULL));
                                        if (t97) {
                                            uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 16));
                                            t97 = (uint8_t)(t98 == t95);
                                        }
                                        uint8_t t99 = (uint8_t)(t97);
                                        if (!t99) {
                                            goto gm_lb6;
                                        }
                                    }
                                    uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)&F.l9) + 40) + 8));
                                    uint8_t t101 = ((t100 == ((uint64_t)0ULL)));
                                    if (!t101) {
                                        goto gm_lb6;
                                    }
                                    {
                                        uint32_t t102 = gm_l2;
                                        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                                        uint8_t t104 = (uint8_t)(t103 == ((uint8_t)0ULL));
                                        if (t104) {
                                            uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
                                            t104 = (uint8_t)(t105 == t102);
                                        }
                                        uint8_t t106 = (uint8_t)(t104);
                                        if (!t106) {
                                            goto gm_lb6;
                                        }
                                    }
                                    {
                                        void * t107 = F.l0;
                                        void * t108 = (void *)(((uint8_t *)t107) + 96);
                                        gm_tmpdn4irouh8b t109;
                                        gm_f_pop_2qgfbfe((void *)&t109, t108);
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
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
                        void * t110 = F.l0;
                        uint32_t t111 = gm_l2;
                        gm_tlg23kb3oj23s t112;
                        gm_f_local_place_qnf3mb2((void *)&t112, t111);
                        gm_t7b7esi3n9r90 t113 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                        gm_t6fp3q71vjrmk t114;
                        GM_ST(uint64_t, (uint8_t *)&t114 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t114 + 0), t112);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t114 + 64), t113);
                        void * t115 = gm_l4;
                        gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t115) + 72));
                        gm_t8658kcqovtpe t117;
                        memset(&t117, 0, sizeof t117);
                        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t117 + 16), t114);
                        gm_b0_1 t118 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t110, &t117, t116);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t120 = (uint8_t)(t119 == ((uint8_t)1ULL));
                    if (!t120) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 8));
                    gm_l10 = t121;
                    void * t122 = GM_LD(void *, ((((uint8_t *)t49) + 8) + 16));
                    gm_l11 = t122;
                    {
                        void * t123 = F.l0;
                        void * t124 = gm_l11;
                        gm_f_operand_place_cdle4ls((void *)&gm_l63, (void *)NULL, cx, NULL, t123, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t125 = (uint8_t)((((uint8_t *)&gm_l63))[56] != 0);
                    if (t125) {
                        memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l63), 56);
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t126 = F.l0;
                        void * t127 = GM_LD(void *, ((uint8_t *)t126));
                        void * t128 = gm_l11;
                        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t128));
                        gm_f_layout_a5ovnrm((void *)&gm_l64, (void *)NULL, cx, NULL, t127, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 16));
                    gm_l13 = t130;
                    {
                        {
                            void * t131 = F.l0;
                            void * t132 = GM_LD(void *, ((uint8_t *)t131));
                            void * t133 = (void *)(((uint8_t *)t132) + 8);
                            void * t134 = gm_l11;
                            gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t134));
                            gm_f_kind_jcnt4hm((void *)&gm_l65, t133, GM_CONV(gm_csguankko4lmhd, t135));
                            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l65) + 0));
                            uint8_t t137 = (uint8_t)(t136 == ((uint8_t)12ULL));
                            if (!t137) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                            gm_l14 = t138;
                            gm_b16_8 t139 = gm_l10;
                            gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                            gm_l15 = t140;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t142 = gm_l16;
                                    uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t144 = (t142 < t143);
                                    if (!t144) {
                                        goto gm_lb10;
                                    }
                                    {
                                        uint64_t t145 = gm_l16;
                                        void * t146 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t148 = gm_l16;
                                        gm_t9805qtonbsag t149 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t146) + (uintptr_t)t148 * 8));
                                        gm_tp0pp2cmr6tvd t150;
                                        GM_ST(uint64_t, ((uint8_t *)&t150 + 0), t145);
                                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t150 + 8), t149);
                                        gm_l66 = t150;
                                        {
                                            {
                                                uint64_t t151 = GM_LD(uint64_t, ((uint8_t *)&gm_l66));
                                                gm_l17 = t151;
                                                gm_t9805qtonbsag t152 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l66) + 8));
                                                gm_l18 = t152;
                                                uint8_t t153 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
                                                if (t153) {
                                                    uint32_t t154 = GM_LD(uint32_t, ((uint8_t *)&gm_l18));
                                                    gm_l19 = t154;
                                                } else {
                                                    goto gm_lb12;
                                                }
                                                {
                                                    void * t155 = F.l0;
                                                    void * t156 = GM_LD(void *, ((uint8_t *)t155));
                                                    void * t157 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                    uint64_t t159 = gm_l17;
                                                    if ((uintptr_t)t159 >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)t159, t158, gm_srccujoh2vgr9aq, 755, 32);
                                                    gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t157) + (uintptr_t)t159 * 4));
                                                    gm_f_layout_a5ovnrm((void *)&gm_l67, (void *)NULL, cx, NULL, t156, t160);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll141;
                                                    }
                                                }
                                                uint64_t t161 = GM_LD(uint64_t, ((uint8_t *)&gm_l67));
                                                uint8_t t162 = ((t161 == ((uint64_t)0ULL)));
                                                if (t162) {
                                                    goto gm_lb12;
                                                }
                                                {
                                                    void * t163 = F.l0;
                                                    uint64_t t164 = gm_l17;
                                                    gm_tlg23kb3oj23s t165 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l12));
                                                    uint32_t t166 = (uint32_t)t164;
                                                    void * t167 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                    uint64_t t169 = gm_l17;
                                                    if ((uintptr_t)t169 >= (uintptr_t)(t168)) gm_trap_index((uintptr_t)t169, t168, gm_srccujoh2vgr9aq, 756, 62);
                                                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)t167) + (uintptr_t)t169 * 8));
                                                    gm_tb31k5l794t7p t171;
                                                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                                    GM_ST(uint32_t, ((uint8_t *)&t171 + 0), t166);
                                                    GM_ST(uint64_t, ((uint8_t *)&t171 + 8), t170);
                                                    gm_tfqmtuprvunod t172;
                                                    memset(&t172, 0, sizeof t172);
                                                    GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t172 + 16), t171);
                                                    gm_tlg23kb3oj23s t173;
                                                    gm_f_project_6hpd48e((void *)&t173, (void *)NULL, cx, NULL, &t165, &t172);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll141;
                                                    }
                                                    void * t174 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                    uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                    uint64_t t176 = gm_l17;
                                                    if ((uintptr_t)t176 >= (uintptr_t)(t175)) gm_trap_index((uintptr_t)t176, t175, gm_srccujoh2vgr9aq, 756, 73);
                                                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t174) + (uintptr_t)t176 * 4));
                                                    void * t178 = gm_l11;
                                                    gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
                                                    gm_f_load_go4fgkp((void *)&F.l20, (void *)NULL, cx, NULL, t163, &t173, t177, t179);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll141;
                                                    }
                                                }
                                                {
                                                    void * t180 = F.l0;
                                                    uint32_t t181 = gm_l19;
                                                    gm_tlg23kb3oj23s t182;
                                                    gm_f_local_place_qnf3mb2((void *)&t182, t181);
                                                    gm_t7b7esi3n9r90 t183 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l20));
                                                    gm_t6fp3q71vjrmk t184;
                                                    GM_ST(uint64_t, (uint8_t *)&t184 + 56, 0);
                                                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t184 + 0), t182);
                                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t184 + 64), t183);
                                                    void * t185 = gm_l11;
                                                    gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t185) + 72));
                                                    gm_t8658kcqovtpe t187;
                                                    memset(&t187, 0, sizeof t187);
                                                    GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)25ULL));
                                                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t187 + 16), t184);
                                                    gm_b0_1 t188 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t180, &t187, t186);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll141;
                                                    }
                                                }
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 753, 13);
                                        }
gm_lb13:;
                                    }
gm_lb12:;
                                    uint64_t t189 = gm_l16;
                                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                    uint64_t t192 = r190;
                                    gm_l16 = t192;
                                }
                            }
                            goto gm_ls193;
gm_ll141:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb10; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb12; }
                            }
                            goto gm_lad;
gm_ls193:;
gm_lb10:;
                            goto gm_lb8;
                        }
gm_lb9:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb8:;
                    goto gm_lb1;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)2ULL));
                    if (!t195) {
                        goto gm_lb15;
                    }
                    void * t196 = GM_LD(void *, (((uint8_t *)t49) + 8));
                    gm_l21 = t196;
                    void * t197 = GM_LD(void *, ((((uint8_t *)t49) + 8) + 8));
                    gm_l22 = t197;
                    void * t198 = F.l0;
                    void * t199 = gm_l21;
                    uint8_t t200 = gm_f_bits_field_cdqu5on(t198, t199);
                    if (t200) {
                        {
                            gm_trqfvmjtt4sij t201;
                            gm_f_list_s1kpkpr((void *)&t201, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l23 = t201;
                        }
                        void * t202 = gm_l21;
                        gm_l24 = t202;
                        {
                            for (;;) {
                                void * t204 = F.l0;
                                void * t205 = gm_l24;
                                uint8_t t206 = gm_f_bits_field_cdqu5on(t204, t205);
                                if (!t206) {
                                    goto gm_lb16;
                                }
                                {
                                    void * t207 = (void *)((uint8_t *)&F.l23);
                                    void * t208 = gm_l24;
                                    gm_tftb1odc739t5 t209 = gm_f_push_pf02nv6(t207, t208);
                                    gm_l68 = t209;
                                    uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                    if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srccujoh2vgr9aq, 768, 21);
                                    {
                                        {
                                            void * t211 = gm_l24;
                                            uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t211) + 8) + 0));
                                            uint8_t t213 = (uint8_t)(t212 == ((uint8_t)19ULL));
                                            if (!t213) {
                                                goto gm_lb20;
                                            }
                                            void * t214 = GM_LD(void *, ((((uint8_t *)t211) + 8) + 8));
                                            gm_l25 = t214;
                                            void * t215 = gm_l25;
                                            gm_l24 = t215;
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                        goto gm_lb16;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                            }
                        }
gm_lb16:;
                        {
                            void * t216 = F.l0;
                            void * t217 = gm_l24;
                            gm_f_place_oohr45n((void *)&gm_l69, (void *)NULL, cx, NULL, t216, t217);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t218 = (uint8_t)((((uint8_t *)&gm_l69))[56] != 0);
                        if (t218) {
                            memmove(((uint8_t *)&F.l26), ((uint8_t *)&gm_l69), 56);
                        } else {
                            void * t219 = F.l0;
                            void * t220 = gm_l1;
                            void * t221 = gm_l21;
                            gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t221) + 72));
                            gm_b0_1 t223 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t219, t220, t222);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        {
                            void * t224 = F.l0;
                            void * t225 = gm_l22;
                            gm_f_expr_tp86bm9((void *)&F.l27, (void *)NULL, cx, NULL, t224, t225);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t226 = F.l0;
                        void * t227 = gm_l22;
                        uint8_t t228 = gm_f_diverges_p3okmol(t226, t227);
                        if (t228) {
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        {
                            gm_trqfvmjtt4sij t229;
                            gm_f_list_0202sdh((void *)&t229, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l28 = t229;
                        }
                        {
                            void * t230 = (void *)((uint8_t *)&F.l28);
                            void * t231 = F.l0;
                            void * t232 = gm_l24;
                            gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t232));
                            void * t234 = gm_l21;
                            gm_tl874t2atnlh3 t235 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t234) + 72));
                            gm_t7b7esi3n9r90 t236;
                            gm_f_load_go4fgkp((void *)&t236, (void *)NULL, cx, NULL, t231, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l26)), t233, t235);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t237 = gm_f_push_0pgacsj(t230, &t236);
                            gm_l70 = t237;
                        }
                        uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                        if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srccujoh2vgr9aq, 780, 17);
                        uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                        gm_l29 = t239;
                        {
                            for (;;) {
                                uint64_t t241 = gm_l29;
                                uint8_t t242 = (t241 > ((uint64_t)1ULL));
                                if (!t242) {
                                    goto gm_lb21;
                                }
                                {
                                    uint64_t t243 = gm_l29;
                                    uint64_t r244; bool o245 = __builtin_sub_overflow(t243, ((uint64_t)1ULL), &r244);
                                    if (o245) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 783, 21);
                                    uint64_t t246 = r244;
                                    gm_l29 = t246;
                                    {
                                        {
                                            void * t247 = GM_LD(void *, ((uint8_t *)&F.l23));
                                            uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                                            uint64_t t249 = gm_l29;
                                            if ((uintptr_t)t249 >= (uintptr_t)(t248)) gm_trap_index((uintptr_t)t249, t248, gm_srccujoh2vgr9aq, 784, 41);
                                            void * t250 = GM_LD(void *, (((uint8_t *)t247) + (uintptr_t)t249 * 8));
                                            uint8_t t251 = GM_LD(uint8_t, ((((uint8_t *)t250) + 8) + 0));
                                            uint8_t t252 = (uint8_t)(t251 == ((uint8_t)19ULL));
                                            if (!t252) {
                                                goto gm_lb25;
                                            }
                                            uint32_t t253 = GM_LD(uint32_t, (((((uint8_t *)t250) + 8) + 8) + 8));
                                            gm_l30 = t253;
                                            void * t254 = F.l0;
                                            void * t255 = GM_LD(void *, ((uint8_t *)&F.l23));
                                            uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                                            uint64_t t257 = gm_l29;
                                            if ((uintptr_t)t257 >= (uintptr_t)(t256)) gm_trap_index((uintptr_t)t257, t256, gm_srccujoh2vgr9aq, 785, 34);
                                            void * t258 = GM_LD(void *, (((uint8_t *)t255) + (uintptr_t)t257 * 8));
                                            gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t258));
                                            uint32_t t260 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t254, t259);
                                            if (gm_unw.kind) {
                                                goto gm_ll240;
                                            }
                                            gm_l31 = t260;
                                            {
                                                void * t261 = F.l0;
                                                uint32_t t262 = gm_l31;
                                                void * t263 = GM_LD(void *, ((uint8_t *)&F.l28));
                                                uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
                                                uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
                                                uint64_t r266; bool o267 = __builtin_sub_overflow(t265, ((uint64_t)1ULL), &r266);
                                                if (o267) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 786, 54);
                                                uint64_t t268 = r266;
                                                if ((uintptr_t)t268 >= (uintptr_t)(t264)) gm_trap_index((uintptr_t)t268, t264, gm_srccujoh2vgr9aq, 786, 41);
                                                uint32_t t269 = gm_l30;
                                                gm_t7b7esi3n9r90 t270 = GM_LD(gm_t7b7esi3n9r90, (((uint8_t *)t263) + (uintptr_t)t268 * 48));
                                                gm_tcpisgbff1dqv t271;
                                                GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t271 + 8, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t271 + 68, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t271 + 72, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t271 + 0), t262);
                                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t271 + 16), t270);
                                                GM_ST(uint32_t, ((uint8_t *)&t271 + 64), t269);
                                                void * t272 = gm_l21;
                                                gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t272) + 72));
                                                gm_t8658kcqovtpe t274;
                                                memset(&t274, 0, sizeof t274);
                                                GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)21ULL));
                                                GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t274 + 16), t271);
                                                gm_b0_1 t275 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t261, &t274, t273);
                                                if (gm_unw.kind) {
                                                    goto gm_ll240;
                                                }
                                            }
                                            {
                                                void * t276 = (void *)((uint8_t *)&F.l28);
                                                uint32_t t277 = gm_l31;
                                                gm_t7b7esi3n9r90 t278;
                                                memset(&t278, 0, sizeof t278);
                                                GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)0ULL));
                                                GM_ST(uint32_t, ((uint8_t *)&t278 + 16), t277);
                                                gm_tftb1odc739t5 t279 = gm_f_push_0pgacsj(t276, &t278);
                                                gm_l71 = t279;
                                            }
                                            uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                            if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srccujoh2vgr9aq, 787, 21);
                                            goto gm_lb24;
                                        }
gm_lb25:;
                                        goto gm_lb21;
                                    }
gm_lb24:;
                                }
gm_lb23:;
                            }
                        }
                        goto gm_ls281;
gm_ll240:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb21; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb23; }
                        }
                        goto gm_lad;
gm_ls281:;
gm_lb21:;
                        {
                            memmove(((uint8_t *)&F.l32), ((uint8_t *)&F.l27), 48);
                        }
                        gm_l34 = ((uint64_t)0ULL);
                        uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                        gm_l33 = t282;
                        {
                            for (;;) {
                                uint64_t t284 = gm_l34;
                                uint64_t t285 = gm_l33;
                                uint8_t t286 = (t284 < t285);
                                if (!t286) {
                                    goto gm_lb26;
                                }
                                {
                                    {
                                        {
                                            uint64_t t287 = gm_l34;
                                            gm_l35 = t287;
                                            {
                                                {
                                                    void * t288 = GM_LD(void *, ((uint8_t *)&F.l23));
                                                    uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                                                    uint64_t t290 = gm_l35;
                                                    if ((uintptr_t)t290 >= (uintptr_t)(t289)) gm_trap_index((uintptr_t)t290, t289, gm_srccujoh2vgr9aq, 792, 44);
                                                    void * t291 = GM_LD(void *, (((uint8_t *)t288) + (uintptr_t)t290 * 8));
                                                    uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t291) + 8) + 0));
                                                    uint8_t t293 = (uint8_t)(t292 == ((uint8_t)19ULL));
                                                    if (!t293) {
                                                        goto gm_lb32;
                                                    }
                                                    void * t294 = GM_LD(void *, ((((uint8_t *)t291) + 8) + 8));
                                                    gm_l36 = t294;
                                                    uint32_t t295 = GM_LD(uint32_t, (((((uint8_t *)t291) + 8) + 8) + 8));
                                                    gm_l37 = t295;
                                                    void * t296 = F.l0;
                                                    void * t297 = gm_l36;
                                                    gm_t88rcucg1b7uk t298 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t297));
                                                    uint32_t t299 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t296, t298);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll283;
                                                    }
                                                    gm_l38 = t299;
                                                    {
                                                        void * t300 = F.l0;
                                                        uint32_t t301 = gm_l38;
                                                        void * t302 = GM_LD(void *, ((uint8_t *)&F.l28));
                                                        uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
                                                        uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                                                        uint64_t r305; bool o306 = __builtin_sub_overflow(t304, ((uint64_t)1ULL), &r305);
                                                        if (o306) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 794, 54);
                                                        uint64_t t307 = r305;
                                                        uint64_t t308 = gm_l35;
                                                        uint64_t r309; bool o310 = __builtin_sub_overflow(t307, t308, &r309);
                                                        if (o310) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 794, 54);
                                                        uint64_t t311 = r309;
                                                        if ((uintptr_t)t311 >= (uintptr_t)(t303)) gm_trap_index((uintptr_t)t311, t303, gm_srccujoh2vgr9aq, 794, 41);
                                                        uint32_t t312 = gm_l37;
                                                        gm_t7b7esi3n9r90 t313 = GM_LD(gm_t7b7esi3n9r90, (((uint8_t *)t302) + (uintptr_t)t311 * 48));
                                                        gm_t7b7esi3n9r90 t314 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l32));
                                                        gm_t8rrrcunvsibs t315;
                                                        GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t315 + 8, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t315 + 68, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t315 + 72, 0);
                                                        GM_ST(uint32_t, ((uint8_t *)&t315 + 0), t301);
                                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t315 + 16), t313);
                                                        GM_ST(uint32_t, ((uint8_t *)&t315 + 64), t312);
                                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t315 + 80), t314);
                                                        void * t316 = gm_l21;
                                                        gm_tl874t2atnlh3 t317 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t316) + 72));
                                                        gm_t8658kcqovtpe t318;
                                                        memset(&t318, 0, sizeof t318);
                                                        GM_ST(uint8_t, ((uint8_t *)&t318 + 0), ((uint8_t)22ULL));
                                                        GM_ST(gm_t8rrrcunvsibs, ((uint8_t *)&t318 + 16), t315);
                                                        gm_b0_1 t319 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t300, &t318, t317);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll283;
                                                        }
                                                    }
                                                    {
                                                        uint32_t t320 = gm_l38;
                                                        memset(&F.l32, 0, sizeof F.l32);
                                                        GM_ST(uint8_t, ((uint8_t *)&F.l32 + 0), ((uint8_t)0ULL));
                                                        GM_ST(uint32_t, ((uint8_t *)&F.l32 + 16), t320);
                                                    }
                                                    goto gm_lb31;
                                                }
gm_lb32:;
                                                goto gm_lb26;
                                            }
gm_lb31:;
                                            goto gm_lb29;
                                        }
gm_lb30:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 791, 17);
                                    }
gm_lb29:;
                                }
gm_lb28:;
                                uint64_t t321 = gm_l34;
                                uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                                uint64_t t324 = r322;
                                gm_l34 = t324;
                            }
                        }
                        goto gm_ls325;
gm_ll283:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb26; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb28; }
                        }
                        goto gm_lad;
gm_ls325:;
gm_lb26:;
                        {
                            void * t326 = F.l0;
                            gm_tlg23kb3oj23s t327 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l26));
                            gm_t7b7esi3n9r90 t328 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l32));
                            gm_t6fp3q71vjrmk t329;
                            GM_ST(uint64_t, (uint8_t *)&t329 + 56, 0);
                            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t329 + 0), t327);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t329 + 64), t328);
                            void * t330 = gm_l22;
                            gm_tl874t2atnlh3 t331 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t330) + 72));
                            gm_t8658kcqovtpe t332;
                            memset(&t332, 0, sizeof t332);
                            GM_ST(uint8_t, ((uint8_t *)&t332 + 0), ((uint8_t)25ULL));
                            GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t332 + 16), t329);
                            gm_b0_1 t333 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t326, &t332, t331);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t334 = F.l0;
                        void * t335 = gm_l21;
                        gm_f_place_oohr45n((void *)&gm_l72, (void *)NULL, cx, NULL, t334, t335);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t336 = (uint8_t)((((uint8_t *)&gm_l72))[56] != 0);
                    if (t336) {
                        memmove(((uint8_t *)&F.l39), ((uint8_t *)&gm_l72), 56);
                    } else {
                        void * t337 = F.l0;
                        void * t338 = gm_l1;
                        void * t339 = gm_l21;
                        gm_tl874t2atnlh3 t340 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t339) + 72));
                        gm_b0_1 t341 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t337, t338, t340);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t342 = F.l0;
                        void * t343 = gm_l22;
                        gm_f_expr_tp86bm9((void *)&F.l40, (void *)NULL, cx, NULL, t342, t343);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t344 = F.l0;
                    void * t345 = gm_l22;
                    uint8_t t346 = gm_f_diverges_p3okmol(t344, t345);
                    if (t346) {
                        gm_l73 = ((uint8_t)1);
                    } else {
                        void * t347 = F.l0;
                        void * t348 = GM_LD(void *, ((uint8_t *)t347));
                        void * t349 = gm_l22;
                        gm_t88rcucg1b7uk t350 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t349));
                        gm_f_layout_a5ovnrm((void *)&gm_l74, (void *)NULL, cx, NULL, t348, t350);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t351 = GM_LD(uint64_t, ((uint8_t *)&gm_l74));
                        uint8_t t352 = ((t351 == ((uint64_t)0ULL)));
                        gm_l73 = t352;
                    }
                    uint8_t t353 = gm_l73;
                    if (t353) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        void * t354 = F.l0;
                        gm_tlg23kb3oj23s t355 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l39));
                        gm_t7b7esi3n9r90 t356 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l40));
                        gm_t6fp3q71vjrmk t357;
                        GM_ST(uint64_t, (uint8_t *)&t357 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t357 + 0), t355);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t357 + 64), t356);
                        void * t358 = gm_l22;
                        gm_tl874t2atnlh3 t359 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t358) + 72));
                        gm_t8658kcqovtpe t360;
                        memset(&t360, 0, sizeof t360);
                        GM_ST(uint8_t, ((uint8_t *)&t360 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t360 + 16), t357);
                        gm_b0_1 t361 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t354, &t360, t359);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t363 = (uint8_t)(t362 == ((uint8_t)3ULL));
                    if (!t363) {
                        goto gm_lb33;
                    }
                    gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 8));
                    gm_l41 = t364;
                    void * t365 = GM_LD(void *, ((((uint8_t *)t49) + 8) + 16));
                    gm_l42 = t365;
                    {
                        gm_trqfvmjtt4sij t366;
                        gm_f_list_mgqfh8v((void *)&t366, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l43 = t366;
                    }
                    gm_b16_8 t367 = gm_l41;
                    gm_l44 = t367;
                    gm_l45 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t369 = gm_l45;
                            uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint8_t t371 = (t369 < t370);
                            if (!t371) {
                                goto gm_lb34;
                            }
                            {
                                void * t372 = GM_LD(void *, ((uint8_t *)&gm_l44));
                                uint64_t t373 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                uint64_t t374 = gm_l45;
                                {
                                    {
                                        void * t375 = GM_LD(void *, (((uint8_t *)t372) + (uintptr_t)t374 * 8));
                                        gm_l46 = t375;
                                        uint8_t t376 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l46)) != 0);
                                        if (t376) {
                                            void * t377 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                            gm_l47 = t377;
                                        } else {
                                            void * t378 = (void *)((uint8_t *)&F.l43);
                                            gm_tfonjjajjj3jq t379;
                                            memset(&t379, 0, sizeof t379);
                                            gm_tftb1odc739t5 t380 = gm_f_push_2v345ri(t378, &t379);
                                            gm_l75 = t380;
                                            uint8_t t381 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                            if (!t381) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srccujoh2vgr9aq, 814, 21);
                                            goto gm_lb36;
                                        }
                                        void * t382 = F.l0;
                                        void * t383 = gm_l47;
                                        uint8_t t384 = gm_f_bits_field_cdqu5on(t382, t383);
                                        if (t384) {
                                            void * t385 = F.l0;
                                            void * t386 = gm_l1;
                                            void * t387 = gm_l42;
                                            gm_tl874t2atnlh3 t388 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t387) + 72));
                                            gm_b0_1 t389 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t385, t386, t388);
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                            F.ret = GM_ZERO(gm_b0_1);
                                            goto gm_fret;
                                        }
                                        {
                                            void * t390 = (void *)((uint8_t *)&F.l43);
                                            void * t391 = F.l0;
                                            void * t392 = gm_l47;
                                            gm_f_place_oohr45n((void *)&gm_l77, (void *)NULL, cx, NULL, t391, t392);
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                            uint8_t t393 = (uint8_t)((((uint8_t *)&gm_l77))[56] != 0);
                                            if (t393) {
                                                memmove(((uint8_t *)&gm_l76), ((uint8_t *)&gm_l77), 56);
                                            } else {
                                                void * t394 = F.l0;
                                                void * t395 = gm_l1;
                                                void * t396 = gm_l42;
                                                gm_tl874t2atnlh3 t397 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t396) + 72));
                                                gm_b0_1 t398 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t394, t395, t397);
                                                if (gm_unw.kind) {
                                                    goto gm_ll368;
                                                }
                                                F.ret = GM_ZERO(gm_b0_1);
                                                goto gm_fret;
                                            }
                                            gm_tlg23kb3oj23s t399 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&gm_l76));
                                            gm_tfonjjajjj3jq t400;
                                            GM_ST(uint8_t, (uint8_t *)&t400 + 57, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t400 + 58, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t400 + 60, 0);
                                            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t400), t399);
                                            ((uint8_t *)&t400)[56] = 1;
                                            gm_tftb1odc739t5 t401 = gm_f_push_2v345ri(t390, &t400);
                                            gm_l78 = t401;
                                        }
                                        uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                        if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srccujoh2vgr9aq, 821, 17);
                                        goto gm_lb37;
                                    }
gm_lb38:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 812, 13);
                                }
gm_lb37:;
                            }
gm_lb36:;
                            uint64_t t403 = gm_l45;
                            uint64_t r404; bool o405 = __builtin_add_overflow(t403, ((uint64_t)1ULL), &r404);
                            uint64_t t406 = r404;
                            gm_l45 = t406;
                        }
                    }
                    goto gm_ls407;
gm_ll368:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb34; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb36; }
                    }
                    goto gm_lad;
gm_ls407:;
gm_lb34:;
                    {
                        void * t408 = F.l0;
                        void * t409 = gm_l42;
                        gm_f_expr_tp86bm9((void *)&F.l48, (void *)NULL, cx, NULL, t408, t409);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t410 = F.l0;
                    void * t411 = gm_l42;
                    uint8_t t412 = gm_f_diverges_p3okmol(t410, t411);
                    if (t412) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t413 = F.l0;
                    void * t414 = gm_l42;
                    gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t414));
                    uint32_t t416 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t413, t415);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t416;
                    {
                        void * t417 = F.l0;
                        uint32_t t418 = gm_l49;
                        gm_tlg23kb3oj23s t419;
                        gm_f_local_place_qnf3mb2((void *)&t419, t418);
                        gm_t7b7esi3n9r90 t420 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l48));
                        gm_t6fp3q71vjrmk t421;
                        GM_ST(uint64_t, (uint8_t *)&t421 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t421 + 0), t419);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t421 + 64), t420);
                        void * t422 = gm_l42;
                        gm_tl874t2atnlh3 t423 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t422) + 72));
                        gm_t8658kcqovtpe t424;
                        memset(&t424, 0, sizeof t424);
                        GM_ST(uint8_t, ((uint8_t *)&t424 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t424 + 16), t421);
                        gm_b0_1 t425 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t417, &t424, t423);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t426 = F.l0;
                        void * t427 = GM_LD(void *, ((uint8_t *)t426));
                        void * t428 = gm_l42;
                        gm_t88rcucg1b7uk t429 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t428));
                        gm_f_layout_a5ovnrm((void *)&gm_l79, (void *)NULL, cx, NULL, t427, t429);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l79) + 16));
                    gm_l50 = t430;
                    {
                        {
                            void * t431 = F.l0;
                            void * t432 = GM_LD(void *, ((uint8_t *)t431));
                            void * t433 = (void *)(((uint8_t *)t432) + 8);
                            void * t434 = gm_l42;
                            gm_t88rcucg1b7uk t435 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t434));
                            gm_f_kind_jcnt4hm((void *)&gm_l80, t433, GM_CONV(gm_csguankko4lmhd, t435));
                            uint8_t t436 = GM_LD(uint8_t, (((uint8_t *)&gm_l80) + 0));
                            uint8_t t437 = (uint8_t)(t436 == ((uint8_t)12ULL));
                            if (!t437) {
                                goto gm_lb40;
                            }
                            gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
                            gm_l51 = t438;
                            gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                            gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
                            gm_l52 = t440;
                            gm_l53 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t442 = gm_l53;
                                    uint64_t t443 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                    uint8_t t444 = (t442 < t443);
                                    if (!t444) {
                                        goto gm_lb41;
                                    }
                                    {
                                        {
                                            uint64_t t445 = gm_l53;
                                            void * t446 = GM_LD(void *, ((uint8_t *)&gm_l52));
                                            uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                            uint64_t t448 = gm_l53;
                                            gm_tfonjjajjj3jq t449 = GM_LD(gm_tfonjjajjj3jq, (((uint8_t *)t446) + (uintptr_t)t448 * 64));
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l81 + 0), t445);
                                            GM_ST(gm_tfonjjajjj3jq, ((uint8_t *)&gm_l81 + 8), t449);
                                        }
                                        {
                                            {
                                                uint64_t t450 = GM_LD(uint64_t, ((uint8_t *)&gm_l81));
                                                gm_l54 = t450;
                                                {
                                                    memmove(((uint8_t *)&F.l55), (((uint8_t *)&gm_l81) + 8), 64);
                                                }
                                                uint8_t t451 = (uint8_t)((((uint8_t *)&F.l55))[56] != 0);
                                                if (t451) {
                                                    memmove(((uint8_t *)&F.l56), ((uint8_t *)&F.l55), 56);
                                                } else {
                                                    goto gm_lb43;
                                                }
                                                {
                                                    void * t452 = F.l0;
                                                    void * t453 = GM_LD(void *, ((uint8_t *)t452));
                                                    void * t454 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                                    uint64_t t455 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                                    uint64_t t456 = gm_l54;
                                                    if ((uintptr_t)t456 >= (uintptr_t)(t455)) gm_trap_index((uintptr_t)t456, t455, gm_srccujoh2vgr9aq, 834, 32);
                                                    gm_t88rcucg1b7uk t457 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t454) + (uintptr_t)t456 * 4));
                                                    gm_f_layout_a5ovnrm((void *)&gm_l82, (void *)NULL, cx, NULL, t453, t457);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll441;
                                                    }
                                                }
                                                uint64_t t458 = GM_LD(uint64_t, ((uint8_t *)&gm_l82));
                                                uint8_t t459 = ((t458 == ((uint64_t)0ULL)));
                                                if (t459) {
                                                    goto gm_lb43;
                                                }
                                                {
                                                    void * t460 = F.l0;
                                                    uint32_t t461 = gm_l49;
                                                    gm_tlg23kb3oj23s t462;
                                                    gm_f_local_place_qnf3mb2((void *)&t462, t461);
                                                    uint64_t t463 = gm_l54;
                                                    uint32_t t464 = (uint32_t)t463;
                                                    void * t465 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                                    uint64_t t466 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                                    uint64_t t467 = gm_l54;
                                                    if ((uintptr_t)t467 >= (uintptr_t)(t466)) gm_trap_index((uintptr_t)t467, t466, gm_srccujoh2vgr9aq, 835, 75);
                                                    uint64_t t468 = GM_LD(uint64_t, (((uint8_t *)t465) + (uintptr_t)t467 * 8));
                                                    gm_tb31k5l794t7p t469;
                                                    GM_ST(uint32_t, (uint8_t *)&t469 + 4, 0);
                                                    GM_ST(uint32_t, ((uint8_t *)&t469 + 0), t464);
                                                    GM_ST(uint64_t, ((uint8_t *)&t469 + 8), t468);
                                                    gm_tfqmtuprvunod t470;
                                                    memset(&t470, 0, sizeof t470);
                                                    GM_ST(uint8_t, ((uint8_t *)&t470 + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t470 + 16), t469);
                                                    gm_tlg23kb3oj23s t471;
                                                    gm_f_project_6hpd48e((void *)&t471, (void *)NULL, cx, NULL, &t462, &t470);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll441;
                                                    }
                                                    void * t472 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                                    uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                                    uint64_t t474 = gm_l54;
                                                    if ((uintptr_t)t474 >= (uintptr_t)(t473)) gm_trap_index((uintptr_t)t474, t473, gm_srccujoh2vgr9aq, 835, 86);
                                                    gm_t88rcucg1b7uk t475 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t472) + (uintptr_t)t474 * 4));
                                                    void * t476 = gm_l42;
                                                    gm_tl874t2atnlh3 t477 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t476) + 72));
                                                    gm_f_load_go4fgkp((void *)&F.l57, (void *)NULL, cx, NULL, t460, &t471, t475, t477);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll441;
                                                    }
                                                }
                                                {
                                                    void * t478 = F.l0;
                                                    gm_tlg23kb3oj23s t479 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l56));
                                                    gm_t7b7esi3n9r90 t480 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l57));
                                                    gm_t6fp3q71vjrmk t481;
                                                    GM_ST(uint64_t, (uint8_t *)&t481 + 56, 0);
                                                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t481 + 0), t479);
                                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t481 + 64), t480);
                                                    void * t482 = gm_l42;
                                                    gm_tl874t2atnlh3 t483 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t482) + 72));
                                                    gm_t8658kcqovtpe t484;
                                                    memset(&t484, 0, sizeof t484);
                                                    GM_ST(uint8_t, ((uint8_t *)&t484 + 0), ((uint8_t)25ULL));
                                                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t484 + 16), t481);
                                                    gm_b0_1 t485 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t478, &t484, t483);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll441;
                                                    }
                                                }
                                                goto gm_lb44;
                                            }
gm_lb45:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 832, 13);
                                        }
gm_lb44:;
                                    }
gm_lb43:;
                                    uint64_t t486 = gm_l53;
                                    uint64_t r487; bool o488 = __builtin_add_overflow(t486, ((uint64_t)1ULL), &r487);
                                    uint64_t t489 = r487;
                                    gm_l53 = t489;
                                }
                            }
                            goto gm_ls490;
gm_ll441:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb41; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb43; }
                            }
                            goto gm_lad;
gm_ls490:;
gm_lb41:;
                            goto gm_lb39;
                        }
gm_lb40:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb39:;
                    goto gm_lb1;
                }
gm_lb33:;
            }
            {
                {
                    uint8_t t491 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t492 = (uint8_t)(t491 == ((uint8_t)4ULL));
                    if (!t492) {
                        goto gm_lb46;
                    }
                    void * t493 = GM_LD(void *, (((uint8_t *)t49) + 8));
                    gm_l58 = t493;
                    void * t494 = F.l0;
                    void * t495 = gm_l58;
                    gm_t7b7esi3n9r90 t496;
                    gm_f_expr_tp86bm9((void *)&t496, (void *)NULL, cx, NULL, t494, t495);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb46:;
                {
                    uint8_t t497 = GM_LD(uint8_t, (((uint8_t *)t49) + 0));
                    uint8_t t498 = (uint8_t)(t497 == ((uint8_t)5ULL));
                    if (!t498) {
                        goto gm_lb47;
                    }
                    uint8_t t499 = GM_LD(uint8_t, (((uint8_t *)t49) + 8));
                    gm_l59 = t499;
                    void * t500 = GM_LD(void *, ((((uint8_t *)t49) + 8) + 8));
                    F.l60 = t500;
                    void * t501 = F.l0;
                    gm_closure t502 = ((gm_closure){(void *)gm_f_fn_4oh4m1v, (void *)&F.h, cx});
                    gm_b16_8 t503 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t501, t502);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l61 = t503;
                    {
                        void * t504 = F.l0;
                        uint8_t t505 = gm_l59;
                        gm_b16_8 t506 = gm_l61;
                        gm_thervm8j23qet t507;
                        GM_ST(uint8_t, (uint8_t *)&t507 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t507 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t507 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t507 + 0), t505);
                        GM_ST(gm_b16_8, ((uint8_t *)&t507 + 8), t506);
                        void * t508 = F.l60;
                        gm_tl874t2atnlh3 t509 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t508) + 72));
                        gm_t8658kcqovtpe t510;
                        memset(&t510, 0, sizeof t510);
                        GM_ST(uint8_t, ((uint8_t *)&t510 + 0), ((uint8_t)33ULL));
                        GM_ST(gm_thervm8j23qet, ((uint8_t *)&t510 + 16), t507);
                        gm_b0_1 t511 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t504, &t510, t509);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb47:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 728, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_stmts_ctl06qt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmts_ctl06qt F;
    void * gm_l0; /* lw */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l3;
                        gm_l4 = t12;
                        void * t13 = gm_l0;
                        void * t14 = gm_l1;
                        void * t15 = GM_LD(void *, ((uint8_t *)t14));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
                        uint64_t t17 = gm_l4;
                        if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srccujoh2vgr9aq, 724, 40);
                        void * t18 = (void *)(((uint8_t *)t15) + (uintptr_t)t17 * 32);
                        gm_b0_1 t19 = gm_f_stmt_k90qtjr((void *)NULL, cx, NULL, t13, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 724, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_result_fiq5f1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, void * a2) {
    struct gm_fr_store_result_fiq5f1j F;
    void * gm_l0; /* lw */
    gm_t9805qtonbsag gm_l1; /* res */
    void * gm_l2; /* e */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_f_expr_tp86bm9((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_l4 = t5;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    void * t7 = gm_l2;
    uint8_t t8 = gm_f_diverges_p3okmol(t6, t7);
    if (t8) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_l4;
        gm_tlg23kb3oj23s t11;
        gm_f_local_place_qnf3mb2((void *)&t11, t10);
        gm_t7b7esi3n9r90 t12 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
        gm_t6fp3q71vjrmk t13;
        GM_ST(uint64_t, (uint8_t *)&t13 + 56, 0);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t13 + 0), t11);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t13 + 64), t12);
        void * t14 = gm_l2;
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 72));
        gm_t8658kcqovtpe t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t16 + 16), t13);
        gm_b0_1 t17 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t9, &t16, t15);
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
inline __attribute__((aligned(4))) uint32_t gm_f_synthetic_t3b2kd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_synthetic_t3b2kd0 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tbccvikg6b96e gm_l2; /* lay */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)t2));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 16);
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_t0o0svomecjg6 t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 28, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t10 + 0), t7);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t8);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t9);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 24), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t10 + 25), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t10 + 26), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t10 + 27), ((uint8_t)1));
        gm_tftb1odc739t5 t11 = gm_f_push_u8i76or(t6, &t10);
        gm_l3 = t11;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 84, 5);
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 16) + 8));
    uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 85, 13);
    uint64_t t17 = r15;
    uint32_t t18 = (uint32_t)t17;
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
extern __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_try_expr_2gjs5f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_try_expr_2gjs5f7 F;
    uint32_t gm_l4; /* target */
    uint32_t gm_l8; /* present */
    gm_b16_8 gm_l11; /* fail */
    GM_STACK_CHECK(&F, 1510u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_t9805qtonbsag gm_l12;
    gm_t53at80pfdus3 gm_l13;
    gm_tfonjjajjj3jq gm_l14;
    gm_tbccvikg6b96e gm_l15;
    void * t5 = F.l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    F.l3 = t6;
    void * t7 = F.l3;
    void * t8 = F.l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 8));
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
    gm_t9805qtonbsag t11 = gm_f_ancestor_o28bfm6(t7, t9, t10);
    gm_l12 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
    if (t12) {
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
        gm_l4 = t13;
    } else {
        void * t14 = F.l0;
        void * t15 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t16 = F.l3;
    void * t17 = GM_LD(void *, (((uint8_t *)t16) + 344));
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 344) + 8));
    uint32_t t19 = gm_l4;
    uint64_t t20 = (uint64_t)t19;
    if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_srccujoh2vgr9aq, 1210, 12);
    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t17) + (uintptr_t)t20 * 136) + 56));
    F.l5 = t21;
    {
        void * t22 = F.l3;
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        void * t24 = GM_LD(void *, ((uint8_t *)&F.l2));
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t24));
        gm_f_kind_jcnt4hm((void *)&gm_l13, t23, GM_CONV(gm_csguankko4lmhd, t25));
    }
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)14ULL));
            if (!t27) {
                goto gm_lb2;
            }
            F.l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            F.l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1211, 18);
    }
gm_lb1:;
    {
        void * t28 = F.l0;
        void * t29 = GM_LD(void *, ((uint8_t *)&F.l2));
        gm_f_operand_place_cdle4ls((void *)&gm_l14, (void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l14))[56] != 0);
    if (t30) {
        memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l14), 56);
    } else {
        void * t31 = F.l0;
        void * t32 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t33 = F.l0;
    void * t34 = F.l3;
    void * t35 = (void *)(((uint8_t *)t34) + 8);
    gm_t88rcucg1b7uk t36 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t37 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t33, t36);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t37;
    {
        void * t38 = F.l0;
        uint32_t t39 = gm_l8;
        uint8_t t40 = F.l6;
        gm_tlg23kb3oj23s t41 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l7));
        gm_t3349g713hmcr t42;
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t42 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 68, 0);
        GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t39);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t42 + 8), t41);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 64), t40);
        void * t43 = F.l1;
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
        gm_t8658kcqovtpe t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)23ULL));
        GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t45 + 16), t42);
        gm_b0_1 t46 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t38, &t45, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t47 = F.l0;
    void * t48 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
    uint64_t t50 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t47, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t50;
    gm_closure t51 = ((gm_closure){(void *)gm_f_leave_ehrof6p, (void *)&F.h, cx});
    F.l10 = t51;
    void * t52 = F.l0;
    gm_closure t53 = ((gm_closure){(void *)gm_f_fn_k0ofpte, (void *)&F.h, cx});
    gm_b16_8 t54 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t52, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t54;
    {
        void * t55 = F.l0;
        uint32_t t56 = gm_l8;
        gm_t7b7esi3n9r90 t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t57 + 16), t56);
        gm_b16_8 t58 = gm_f_empty_slice_uvn29bg();
        gm_b16_8 t59 = gm_l11;
        gm_t1s3s9ds8udn4 t60;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t60 + 0), t57);
        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 48), t58);
        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 64), t59);
        void * t61 = F.l1;
        gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
        gm_t8658kcqovtpe t63;
        memset(&t63, 0, sizeof t63);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t63 + 16), t60);
        gm_b0_1 t64 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t55, &t63, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = F.l3;
        void * t66 = F.l1;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t66));
        gm_f_layout_a5ovnrm((void *)&gm_l15, (void *)NULL, cx, NULL, t65, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
    uint8_t t69 = ((t68 == ((uint64_t)0ULL)));
    if (t69) {
        void * t70 = F.l1;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t70));
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t71));
        goto gm_fdone;
    }
    {
        void * t72 = F.l0;
        uint64_t t73 = F.l9;
        void * t74 = F.l1;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t74));
        gm_tlcm4lvu3g1a8 t76;
        GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t76 + 0), t73);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76 + 8), t75);
        gm_tfqmtuprvunod t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t77 + 16), t76);
        gm_tlg23kb3oj23s t78;
        gm_f_project_6hpd48e((void *)&t78, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l7)), &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t79 = F.l1;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t79));
        void * t81 = F.l1;
        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 72));
        gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t72, &t78, t80, t82);
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
extern __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_with_expr_g16bcnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_with_expr_g16bcnu F;
    void * gm_l1; /* e */
    void * gm_l5; /* st */
    uint32_t gm_l7; /* l */
    gm_b16_8 gm_l8; /* install */
    gm_b16_8 gm_l9; /* body */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t7 = F.l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    F.l3 = t8;
    {
        memset(&F.l4, 0, sizeof F.l4);
    }
    {
        {
            uint8_t t9 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 24)) != 0);
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = GM_LD(void *, (((uint8_t *)&F.l2) + 24));
            gm_l5 = t10;
            {
                void * t11 = F.l0;
                void * t12 = gm_l5;
                gm_t7b7esi3n9r90 t13;
                gm_f_expr_tp86bm9((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&F.l4 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l4 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l4 + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&F.l4 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&F.l4), t13);
                ((uint8_t *)&F.l4)[48] = 1;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = F.l0;
    void * t15 = gm_l1;
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
    gm_t9805qtonbsag t17 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t14, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t17;
    void * t18 = F.l0;
    uint32_t t19 = gm_f_label_3373lil(t18);
    gm_l7 = t19;
    {
        void * t20 = F.l0;
        void * t21 = (void *)(((uint8_t *)t20) + 144);
        uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
        uint32_t t23 = gm_l7;
        uint32_t t24 = gm_l7;
        gm_t9805qtonbsag t25 = F.l6;
        gm_tp4rm55bg84ue t26;
        GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t22);
        GM_ST(uint32_t, ((uint8_t *)&t26 + 4), t23);
        GM_ST(uint32_t, ((uint8_t *)&t26 + 8), t24);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t26 + 12), t25);
        gm_tftb1odc739t5 t27 = gm_f_push_e2qt8as(t21, &t26);
        gm_l10 = t27;
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccujoh2vgr9aq, 1260, 5);
    {
        for (;;) {
            void * t30 = F.l0;
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t30) + 192) + 8));
            uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            uint64_t t33 = (uint64_t)t32;
            uint8_t t34 = (t31 <= t33);
            if (!t34) {
                goto gm_lb3;
            }
            {
                void * t35 = F.l0;
                void * t36 = (void *)(((uint8_t *)t35) + 192);
                gm_t9805qtonbsag t37;
                memset(&t37, 0, sizeof t37);
                gm_tftb1odc739t5 t38 = gm_f_push_r4haklv(t36, t37);
                gm_l11 = t38;
                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srccujoh2vgr9aq, 1261, 52);
            }
gm_lb5:;
        }
    }
gm_lb3:;
    void * t40 = F.l0;
    void * t41 = GM_LD(void *, (((uint8_t *)t40) + 192));
    uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 192) + 8));
    uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint64_t t44 = (uint64_t)t43;
    if ((uintptr_t)t44 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t44, t42, gm_srccujoh2vgr9aq, 1262, 5);
    gm_t9805qtonbsag t45 = F.l6;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t41) + (uintptr_t)t44 * 8), t45);
    void * t46 = F.l0;
    gm_closure t47 = ((gm_closure){(void *)gm_f_fn_mo8l3p0, (void *)&F.h, cx});
    gm_b16_8 t48 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t46, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t48;
    void * t49 = F.l0;
    gm_closure t50 = ((gm_closure){(void *)gm_f_fn_7h6ducr, (void *)&F.h, cx});
    gm_b16_8 t51 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t49, t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t51;
    {
        void * t52 = F.l0;
        uint32_t t53 = gm_l7;
        uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
        gm_b16_8 t55 = gm_l8;
        gm_b16_8 t56 = gm_l9;
        gm_tf8kfhcnval78 t57 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l4));
        gm_tvdggnhat0k99 t58;
        GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 0), t53);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 4), t54);
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t55);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t58 + 32), t57);
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 96), t56);
        void * t59 = gm_l1;
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
        gm_t8658kcqovtpe t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)37ULL));
        GM_ST(gm_tvdggnhat0k99, ((uint8_t *)&t61 + 16), t58);
        gm_b0_1 t62 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t52, &t61, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t63 = F.l0;
        gm_t9805qtonbsag t64 = F.l6;
        void * t65 = gm_l1;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t65));
        void * t67 = gm_l1;
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
        gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t63, t64, t66, t68);
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
inline __attribute__((aligned(4))) void gm_f_word_1sfojus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2) {
    struct gm_fr_word_1sfojus F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* bits */
    uint16_t gm_l3; /* b */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, ((uint8_t *)t11));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l5, t13, GM_CONV(gm_csguankko4lmhd, t14));
    }
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
            if (!t16) {
                goto gm_lb2;
            }
            uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&gm_l5) + 8) + 2));
            gm_l3 = t17;
            uint16_t t18 = gm_l3;
            uint8_t t19 = (t18 > ((uint16_t)64ULL));
            if (t19) {
                memset(&(*(gm_tf8kfhcnval78 *)gm_out), 0, sizeof (*(gm_tf8kfhcnval78 *)gm_out));
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
                    if (!t21) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)4ULL));
                    if (!t25) {
                        goto gm_lb7;
                    }
                    goto gm_lb4;
                }
gm_lb7:;
                goto gm_lb3;
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)13ULL));
            if (!t27) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t28;
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, ((uint8_t *)t29));
            gm_t88rcucg1b7uk t31 = gm_l4;
            uint8_t t32 = gm_f_is_nullable_pointer_nkie06s(t30, GM_CONV(gm_csguankko4lmhd, t31));
            uint8_t t33 = (uint8_t)!t32;
            if (t33) {
                memset(&(*(gm_tf8kfhcnval78 *)gm_out), 0, sizeof (*(gm_tf8kfhcnval78 *)gm_out));
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            memset(&(*(gm_tf8kfhcnval78 *)gm_out), 0, sizeof (*(gm_tf8kfhcnval78 *)gm_out));
            goto gm_fdone;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 160, 5);
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t34 = gm_l1;
        uint64_t t35 = gm_l2;
        gm_tfbnciphf3261 t36;
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 0), t34);
        GM_ST(uint64_t, ((uint8_t *)&t36 + 8), t35);
        gm_t7b7esi3n9r90 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t37 + 16), t36);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out) + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out)), t37);
        ((uint8_t *)&(*(gm_tf8kfhcnval78 *)gm_out))[48] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0);
__attribute__((aligned(4))) gm_t7b7esi3n9r90 gm_f_zero_sln4lkh_c(gm_csguankko4lmhd a0) { gm_t7b7esi3n9r90 gm_r; gm_f_zero_sln4lkh(&gm_r, a0); return gm_r; }

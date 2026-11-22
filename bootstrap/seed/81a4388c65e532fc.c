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
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; void * f2; } gm_t017ud7s0np4m;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t017ud7s0np4m) == 24 && _Alignof(gm_t017ud7s0np4m) == 8 && offsetof(gm_t017ud7s0np4m, f0) == 0 && offsetof(gm_t017ud7s0np4m, f1) == 8 && offsetof(gm_t017ud7s0np4m, f2) == 16, "layout of `(f: u32, sizes: ^Expr, aligns: ^Expr)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
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
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t p3[3]; gm_t88rcucg1b7uk f3; } gm_tcu60n24kv26r;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_b16_8 v1; } f1; } gm_te3magq8drarp;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tj4mbomtvccf0;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_t017ud7s0np4m f0; uint8_t f1; uint8_t pe[7]; } gm_tn6p6lpnve7b0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcu60n24kv26r) == 32 && _Alignof(gm_tcu60n24kv26r) == 8 && offsetof(gm_tcu60n24kv26r, f0) == 0 && offsetof(gm_tcu60n24kv26r, f1) == 8 && offsetof(gm_tcu60n24kv26r, f2) == 24 && offsetof(gm_tcu60n24kv26r, f3) == 28, "layout of `SoaMade`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te3magq8drarp) == 24 && _Alignof(gm_te3magq8drarp) == 8 && offsetof(gm_te3magq8drarp, f0) == 0 && offsetof(gm_te3magq8drarp, f1) == 8, "layout of `SoaTy`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4mbomtvccf0) == 32 && _Alignof(gm_tj4mbomtvccf0) == 8 && offsetof(gm_tj4mbomtvccf0, f0) == 0 && offsetof(gm_tj4mbomtvccf0, f1) == 8 && offsetof(gm_tj4mbomtvccf0, f2) == 24, "layout of `SoaTemplate`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tn6p6lpnve7b0) == 32 && _Alignof(gm_tn6p6lpnve7b0) == 8 && offsetof(gm_tn6p6lpnve7b0, f0) == 0 && offsetof(gm_tn6p6lpnve7b0, f1) == 24, "layout of `?(f: u32, sizes: ^Expr, aligns: ^Expr)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_toa2vhe3l4hfr a[1]; } gm_t2ubvi1j1aq22;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2ubvi1j1aq22) == 40 && _Alignof(gm_t2ubvi1j1aq22) == 8 && offsetof(gm_t2ubvi1j1aq22, a) == 0, "layout of `[1]GenericArg`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_compiler_adt_eiq6rf1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_trqfvmjtt4sij l4; /* defaults */
    gm_trqfvmjtt4sij l5; /* default_exprs */
    gm_t6art7bprjiqb l8; /* info */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_core_fn_of_jr7r6ef {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_empty_slice_1mrcnd6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hp0e9gl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oqjc4ke {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
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
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_format_16dceus {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3dfoht6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4nn4ect {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_agi9f7f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d04m6io {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fambdq4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0i128e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mlqiori {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_thmq1t9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_2 {
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
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_one_arg_f1gj3pp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3dc3qci {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
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
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_j84ih6c {
    gm_frame h;
    gm_t6art7bprjiqb l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_mf0vgpj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vrmo7c5 {
    gm_frame h;
    gm_t0mglksrkfa5e l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_20cc80k {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
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
struct gm_fr_regrow_he53hf7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ht2em0b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q0a57c8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_soa_errors_m1bqopj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    gm_trqfvmjtt4sij l3; /* errors */
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_same_len_bnkvi6r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_set_template_o09q96o {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_soa_block_ir2b1jv {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_column_7a5htvb {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_copy_columns_s4nhe5h {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_t0mglksrkfa5e l13; /* field */
    gm_trqfvmjtt4sij l16; /* pending */
    gm_trqfvmjtt4sij l19; /* args */
    void * ret;
};
struct gm_fr_soa_core_sd8eq62 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* pending */
    gm_tn6p6lpnve7b0 ret;
};
struct gm_fr_soa_expand_2e8rmqf {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_fields_dg06fgc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_free_columns_n1tvn1j {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* args */
    void * ret;
};
struct gm_fr_soa_gather_of_4timqsa {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    gm_t0mglksrkfa5e l10; /* f */
    void * ret;
};
struct gm_fr_soa_generic_kvk21mq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_info_pq4q43l {
    gm_frame h;
    gm_t8cc1mtbrhlpt ret;
};
struct gm_fr_soa_layout_n4ibc7b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* sizes */
    gm_trqfvmjtt4sij l5; /* aligns */
    gm_t0mglksrkfa5e l8; /* f */
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_soa_len_of_4l5vk85 {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_make_columns_ardr0ua {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* args */
    gm_trqfvmjtt4sij l17; /* parts */
    gm_trqfvmjtt4sij l26; /* arms */
    void * ret;
};
struct gm_fr_soa_repeatable_rqn3pcn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_scatter_h5ruqu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_755ppqp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_of_ohenvjc {
    gm_frame h;
    gm_tbm0n9ujhrlhf ret;
};
struct gm_fr_soa_slice_column_6ogdk5f {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_slice_columns_kqq8dfr {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* parts */
    void * ret;
};
struct gm_fr_soa_stand_in_kdtmkn7 {
    gm_frame h;
    gm_t2ubvi1j1aq22 l5; /* <temporary array> */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_temp_2pshn8g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_template_fa784hs {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_to_const_hhiu49k {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* parts */
    void * ret;
};
struct gm_fr_soa_type_42lvsj0 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_expr_go3p0n7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_of_q8vtd5j {
    gm_frame h;
    gm_t6art7bprjiqb l8; /* info */
    gm_tpautr4br7h3l l10; /* def */
    gm_t0mglksrkfa5e l13; /* f */
    gm_trqfvmjtt4sij l18; /* columns */
    gm_t0mglksrkfa5e l21; /* f */
    gm_t2ubvi1j1aq22 l26; /* <temporary array> */
    gm_te3magq8drarp ret;
};
struct gm_fr_subst_soa_2chqte3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_lit_a8hojea {
    gm_frame h;
    void * ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_nt3g9ko(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3dc3qci(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mf0vgpj(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_20cc80k(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_he53hf7(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht2em0b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q0a57c8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9);
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k2ok1rai48t[40] __attribute__((aligned(8)));
extern uint8_t gm_k6tu981lmau[40] __attribute__((aligned(8)));
extern uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s4s2kvgh5crk6[63];
extern const uint8_t gm_s516pv14t5t6i[73];
extern const uint8_t gm_s58rgrgmho9sa[68];
extern const uint8_t gm_s92lujmr0a2c3[35];
extern const uint8_t gm_sfflovsm8rlpo[51];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_sh54qlvpeqnin[10];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_si7vj1dcaqhfd[5];
extern const uint8_t gm_sidairr6d8hcq[45];
extern const uint8_t gm_sk0kqh2pa96ut[73];
extern const uint8_t gm_sloe55935mobb[8];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4nj79mj9jk57[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssmb6ukuk62fe[53];
extern const uint8_t gm_ssnb4or831s5l[78];
extern const uint8_t gm_suh0878eve8t9[10];
extern const gm_tmpl gm_tpl4s7g8d4v4djl;
extern const gm_tmpl gm_tpl7ilj5n9objtq;
extern const gm_tmpl gm_tplaemahh05v026;
extern const gm_tmpl gm_tplb86t2p96msdt;
extern const gm_tmpl gm_tplhr6emralh8q6;
extern const gm_tmpl gm_tpln1554he46gkc;
extern const gm_tmpl gm_tplpsmnsqmt7n3e;
extern const gm_tmpl gm_tpls4d220l28nlk;
extern const gm_tmpl gm_tplt320ru9bfpdd;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_tiilmfd16v3fc t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1mrcnd6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hp0e9gl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oqjc4ke F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    gm_tpautr4br7h3l t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 16), ((uint8_t)0));
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t5 + 24), t3);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t5 + 40), t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0), ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8), t5);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_16dceus F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4s2kvgh5crk6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln1554he46gkc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3dfoht6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk0kqh2pa96ut));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4s7g8d4v4djl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4nn4ect F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssmb6ukuk62fe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt320ru9bfpdd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_agi9f7f F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sou5f0qr4vdgr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhr6emralh8q6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_d04m6io F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si7vj1dcaqhfd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls4d220l28nlk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fambdq4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s516pv14t5t6i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplaemahh05v026), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j0i128e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh54qlvpeqnin));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplb86t2p96msdt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mlqiori F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s92lujmr0a2c3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7ilj5n9objtq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_thmq1t9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sloe55935mobb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpsmnsqmt7n3e), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 136) + 8));
    uint32_t t4 = (uint32_t)t3;
    gm_l2 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 136);
    gm_b16_8 t7 = gm_l1;
    gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src0ih77f43mhf2, 263, 5);
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 176);
    uint32_t t12 = gm_l2;
    gm_tftb1odc739t5 t13 = gm_f_push_im9d4ro(t11, t12);
    gm_l4 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src0ih77f43mhf2, 264, 5);
    uint32_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_arg_f1gj3pp F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_vp1djv1_2((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        void * t4 = gm_l0;
        gm_tftb1odc739t5 t5 = gm_f_push_d943vgp_2(t3, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t4)));
        gm_l2 = t5;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src4nj79mj9jk57, 404, 5);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3dc3qci(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3dc3qci F;
    void * gm_l0; /* l */
    gm_tj4mbomtvccf0 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_hp0e9gl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_20cc80k(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6trg8r7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8r12kcb(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)&F.l1), 88);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_j84ih6c F;
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
        gm_b16_8 t9 = gm_f_empty_slice_oqjc4ke();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q0a57c8(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)&F.l1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mf0vgpj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mf0vgpj F;
    void * gm_l0; /* l */
    gm_tcu60n24kv26r gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_1mrcnd6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_he53hf7(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_8q3kspo_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j7fb9h1_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vrmo7c5 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ose1ba9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ht2em0b(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_20cc80k(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_20cc80k F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_he53hf7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_he53hf7 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht2em0b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ht2em0b F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q0a57c8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q0a57c8 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 272, 5);
    uint32_t t7 = gm_l2;
    GM_ST(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4), t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_fields_dg06fgc F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)a1));
    gm_f_struct_fields_chqu3it((void *)&gm_l3, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l2 = t5;
    } else {
        gm_b16_8 t6 = gm_f_empty_slice_ose1ba9();
        gm_l2 = t6;
    }
    gm_b16_8 t7 = gm_l2;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_generic_kvk21mq F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)a1));
    uint8_t t5 = gm_f_has_param_u9t0efs(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
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
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_usize_lit_a8hojea F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = gm_l1;
    gm_tbci5ospk5nnn t6;
    gm_f_from_u64_ud13q5v((void *)&t6, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t7 = gm_l2;
    gm_to0f5qn75cgg8 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t8 + 8), t6);
    void * t9 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t8, t7);
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
inline __attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3) {
    struct gm_fr_compiler_adt_eiq6rf1 F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    uint64_t gm_l3; /* fields */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_s83jn2q((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t7;
    }
    {
        gm_trqfvmjtt4sij t8;
        gm_f_list_pmp06kq_2((void *)&t8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t8;
    }
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t9 = gm_l3;
    gm_l6 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l7;
            uint64_t t11 = gm_l6;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t13 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t14 = gm_f_push_vd1290p(t13, ((uint8_t)0));
                        gm_l9 = t14;
                        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src4nj79mj9jk57, 141, 9);
                        void * t16 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t17 = gm_f_push_r2k6rjf_2(t16, ((void *)0));
                        gm_l10 = t17;
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src4nj79mj9jk57, 142, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 140, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l7;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l7 = t22;
        }
    }
gm_lb1:;
    {
        gm_b16_8 t23 = gm_l0;
        uint64_t t24 = gm_l1;
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        gm_tf5srh4mp4b3i t26;
        memset(&t26, 0, sizeof t26);
        gm_ti0kgh3pc3in2 t27 = GM_LD(gm_ti0kgh3pc3in2, ((uint8_t *)&F.l2));
        gm_f_new_adt_info_5pl2srl((void *)&F.l8, t23, t24, &t27, GM_CONV(gm_cseahvg8v7p0vs, t25), t26);
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 80), t28);
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 96), t29);
    {
        memmove(gm_out, ((uint8_t *)&F.l8), 184);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_core_fn_of_jr7r6ef F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* mname */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 722u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    gm_t6qooom8qa2do gm_l11;
    gm_b16_8 t9 = gm_l1;
    gm_tnpifv20irnph t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
    gm_f_format_agi9f7f((void *)&gm_l9, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src4nj79mj9jk57, 55, 14);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_l3 = t12;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 336));
    gm_l4 = t15;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l5;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t21 = gm_l5;
                {
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t19) + (uintptr_t)t21 * 8));
                        gm_l6 = t22;
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, ((uint8_t *)t23));
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 200));
                        uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 200) + 8));
                        uint64_t t27 = gm_l6;
                        if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_src4nj79mj9jk57, 57, 12);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t25) + (uintptr_t)t27 * 184));
                        gm_b16_8 t29 = gm_l3;
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                        uint8_t t31 = (!gm_eq0iqiubfm7e(((uint8_t *)&t28), ((uint8_t *)&t30)));
                        if (t31) {
                            goto gm_lb3;
                        }
                        void * t32 = gm_l0;
                        void * t33 = GM_LD(void *, ((uint8_t *)t32));
                        uint64_t t34 = gm_l6;
                        gm_b16_8 t35 = gm_l2;
                        gm_tf5srh4mp4b3i t36 = gm_f_lookup_59juqoo(t33, t34, t35);
                        gm_l10 = t36;
                        uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                        if (t37) {
                            uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            gm_l7 = t38;
                        } else {
                            gm_t9805qtonbsag t39;
                            memset(&t39, 0, sizeof t39);
                            F.ret = t39;
                            goto gm_fret;
                        }
                        {
                            {
                                void * t40 = gm_l0;
                                uint64_t t41 = gm_l7;
                                gm_f_resolve_decl_pus042v((void *)&gm_l11, (void *)NULL, cx, NULL, t40, t41);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)5ULL));
                                if (!t43) {
                                    goto gm_lb7;
                                }
                                uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                                gm_l8 = t44;
                                uint32_t t45 = gm_l8;
                                gm_t9805qtonbsag t46;
                                GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t46), t45);
                                ((uint8_t *)&t46)[4] = 1;
                                F.ret = t46;
                                goto gm_fret;
                            }
gm_lb7:;
                            gm_t9805qtonbsag t47;
                            memset(&t47, 0, sizeof t47);
                            F.ret = t47;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 56, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l5;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l5 = t51;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t52;
    memset(&t52, 0, sizeof t52);
    F.ret = t52;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_report_soa_errors_m1bqopj F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l7;
    gm_tftb1odc739t5 gm_l8;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t7;
    }
    {
        void * t8 = gm_l0;
        memmove(((uint8_t *)&F.l3), (((uint8_t *)t8) + 2576), 40);
    }
    {
        void * t9 = gm_l0;
        gm_trqfvmjtt4sij t10;
        gm_f_list_epvducg((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 2576), t10);
    }
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_l4 = t11;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l5;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t17 = gm_l5;
                {
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)t17 * 16));
                        gm_l6 = t18;
                        gm_b16_8 t19 = gm_l6;
                        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                        uint8_t t21 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t20) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t20))) + i * 16, ((uint8_t *)&t19))) { t21 = 1; break; }
                        uint8_t t22 = (uint8_t)(t21);
                        if (t22) {
                            goto gm_lb3;
                        }
                        void * t23 = gm_l0;
                        gm_tl874t2atnlh3 t24 = gm_l1;
                        gm_b16_8 t25 = gm_l6;
                        gm_tnpifv20irnph t26;
                        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
                        gm_f_format_mlqiori((void *)&gm_l7, (void *)NULL, cx, NULL, t26);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src4nj79mj9jk57, 196, 23);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_b0_1 t30 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t23, t24, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t31 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t32 = gm_l6;
                        gm_tftb1odc739t5 t33 = gm_f_push_3ef746b(t31, t32);
                        gm_l8 = t33;
                        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src4nj79mj9jk57, 197, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 194, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l5;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l5 = t38;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_len_bnkvi6r F;
    gm_tf5srh4mp4b3i gm_l0; /* a */
    gm_tf5srh4mp4b3i gm_l1; /* b */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    {
        {
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l0))[8] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
            gm_l2 = t10;
            {
                {
                    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l1))[8] != 0);
                    if (!t11) {
                        goto gm_lb4;
                    }
                    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
                    gm_l3 = t12;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = gm_l3;
                    uint8_t t15 = ((t13 == t14));
                    F.ret = t15;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l1))[8] != 0);
            if (t16) {
                goto gm_lb6;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 50, 12);
    }
gm_lb5:;
    uint8_t t17 = gm_l4;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_block_ir2b1jv F;
    gm_b16_8 gm_l0; /* stmts */
    void * gm_l1; /* tail */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l3;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        void * t4 = gm_l1;
        F.ret = t4;
        goto gm_fret;
    }
    {
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
        gm_b16_8 t7 = gm_l0;
        void * t8 = gm_l1;
        gm_tl874t2atnlh3 t9 = gm_l2;
        gm_tgabaikr08mjg t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t7);
        GM_ST(void *, ((uint8_t *)&t10 + 16), t8);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 24), t9);
        gm_tq7s98ihonkju t11 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khikhlemh0i)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src4nj79mj9jk57, 238, 35);
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l3) + 8));
        gm_tl874t2atnlh3 t14 = gm_l2;
        gm_to0f5qn75cgg8 t15;
        memset(&t15, 0, sizeof t15);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t15 + 8), t13);
        void * t16 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t6, &t15, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t16;
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
inline __attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_column_7a5htvb F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_t88rcucg1b7uk gm_l6; /* ft */
    void * gm_l7; /* col */
    uint8_t gm_l8; /* is_const */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 805u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_th2lqbo77pvsc gm_l11;
    uint8_t gm_l12;
    gm_t53at80pfdus3 gm_l13;
    {
        void * t7 = gm_l0;
        void * t8 = gm_l1;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t8));
        gm_f_struct_fields_chqu3it((void *)&gm_l11, t7, GM_CONV(gm_csguankko4lmhd, t9));
    }
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
    if (t10) {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l5 = t11;
    } else {
        gm_b16_8 t12 = gm_f_empty_slice_ose1ba9();
        gm_l5 = t12;
    }
    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t15 = gm_l3;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_src4nj79mj9jk57, 258, 11);
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t13) + (uintptr_t)t15 * 40) + 16));
    gm_l6 = t16;
    {
        gm_t88rcucg1b7uk t17 = gm_l6;
        void * t18 = gm_l1;
        void * t19 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t20 = gm_l3;
        uint32_t t21 = (uint32_t)t20;
        gm_tbl8il92l97ut t22;
        GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t22 + 0), t19);
        GM_ST(uint32_t, ((uint8_t *)&t22 + 8), t21);
        gm_tl874t2atnlh3 t23 = gm_l4;
        gm_to0f5qn75cgg8 t24;
        memset(&t24, 0, sizeof t24);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t24 + 8), t22);
        void * t25 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t24, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t25;
    }
    {
        {
            uint8_t t26 = (uint8_t)(((((uint8_t *)a2) + 8))[8] != 0);
            if (t26) {
                goto gm_lb2;
            }
            gm_l12 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l12 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 260, 8);
    }
gm_lb1:;
    uint8_t t27 = gm_l12;
    if (t27) {
        void * t28 = gm_l7;
        F.ret = t28;
        goto gm_fret;
    }
    {
        {
            {
                void * t29 = gm_l0;
                void * t30 = (void *)(((uint8_t *)t29) + 8);
                gm_t88rcucg1b7uk t31 = gm_l6;
                gm_f_kind_jcnt4hm((void *)&gm_l13, t30, GM_CONV(gm_csguankko4lmhd, t31));
            }
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)9ULL));
            if (!t33) {
                goto gm_lb5;
            }
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 8));
            gm_l8 = t34;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l13) + 8) + 4));
            gm_l9 = t35;
            {
                void * t36 = gm_l0;
                void * t37 = (void *)(((uint8_t *)t36) + 8);
                gm_t88rcucg1b7uk t38 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t39 = gm_l1;
                void * t40 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                if (o43) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 262, 64);
                uint64_t t44 = r42;
                uint32_t t45 = (uint32_t)t44;
                gm_tbl8il92l97ut t46;
                GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                GM_ST(void *, ((uint8_t *)&t46 + 0), t40);
                GM_ST(uint32_t, ((uint8_t *)&t46 + 8), t45);
                gm_tl874t2atnlh3 t47 = gm_l4;
                gm_to0f5qn75cgg8 t48;
                memset(&t48, 0, sizeof t48);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t48 + 8), t46);
                void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t38, &t48, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t49;
            }
            {
                void * t50 = gm_l0;
                void * t51 = (void *)(((uint8_t *)t50) + 8);
                uint8_t t52 = gm_l8;
                gm_t88rcucg1b7uk t53 = gm_l9;
                gm_t88rcucg1b7uk t54 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t51, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t55 = gm_l7;
                void * t56 = gm_l0;
                gm_tl874t2atnlh3 t57 = gm_l4;
                void * t58 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t56, ((uint64_t)0ULL), t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t59 = gm_l10;
                gm_tao63pk3r18v5 t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 28, 0);
                GM_ST(void *, ((uint8_t *)&t60 + 0), t55);
                GM_ST(void *, ((uint8_t *)&t60 + 8), t58);
                GM_ST(void *, ((uint8_t *)&t60 + 16), t59);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 24), ((uint8_t)0));
                gm_tl874t2atnlh3 t61 = gm_l4;
                gm_to0f5qn75cgg8 t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t62 + 8), t60);
                void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t62, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t63;
                goto gm_fret;
            }
        }
gm_lb5:;
        void * t64 = gm_l7;
        F.ret = t64;
        goto gm_fret;
    }
gm_lb4:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_copy_columns_s4nhe5h F;
    void * gm_l0; /* s */
    void * gm_l1; /* dst */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* src */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint32_t gm_l6; /* f */
    gm_tjpqvfd72o62u gm_l7; /* from */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    void * gm_l14; /* d */
    void * gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l17; /* <temporary array> */
    uint32_t gm_l18; /* inst */
    GM_STACK_CHECK(&F, 1774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9805qtonbsag gm_l20;
    gm_t8cc1mtbrhlpt gm_l21;
    gm_t8ta482bl6be0 gm_l22;
    uint8_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tq7s98ihonkju gm_l27;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_l5 = t11;
    void * t12 = gm_l0;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_ss496b65r9omj));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(5));
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sgvvkov5kvprt));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(9));
    gm_t9805qtonbsag t15 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l20 = t15;
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l20))[4] != 0);
    if (t16) {
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
        gm_l6 = t17;
    } else {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l4;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sidairr6d8hcq));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(44));
        gm_b0_1 t21 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t22 = gm_l5;
        gm_t88rcucg1b7uk t23 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t24 = gm_f_empty_slice_g3tddrb_2();
        gm_tl874t2atnlh3 t25 = gm_l4;
        gm_to0f5qn75cgg8 t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t24);
        void * t27 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t23, &t26, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t27;
        goto gm_fret;
    }
    {
        void * t28 = gm_l0;
        void * t29 = gm_l3;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t29));
        gm_f_soa_info_pq4q43l((void *)&gm_l21, t28, GM_CONV(gm_csguankko4lmhd, t30));
    }
    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l21))[32] != 0);
    if (t31) {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l21), 32);
    } else {
        void * t32 = gm_l5;
        gm_t88rcucg1b7uk t33 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t34 = gm_f_empty_slice_g3tddrb_2();
        gm_tl874t2atnlh3 t35 = gm_l4;
        gm_to0f5qn75cgg8 t36;
        memset(&t36, 0, sizeof t36);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t34);
        void * t37 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t33, &t36, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t37;
        goto gm_fret;
    }
    {
        void * t38 = gm_l0;
        gm_b16_8 t39 = gm_f_soa_fields_dg06fgc(t38, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l8 = t39;
    }
    {
        gm_trqfvmjtt4sij t40;
        gm_f_list_ir0h3tc_2((void *)&t40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t40;
    }
    gm_b16_8 t41 = gm_l8;
    gm_l10 = t41;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t42 = gm_l11;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t44 = (t42 < t43);
            if (!t44) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t45 = gm_l11;
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t48 = gm_l11;
                    gm_t0mglksrkfa5e t49 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t46) + (uintptr_t)t48 * 40));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l22 + 0), t45);
                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l22 + 8), t49);
                }
                {
                    {
                        uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                        gm_l12 = t50;
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l22) + 8), 40);
                        }
                        {
                            void * t51 = gm_l0;
                            void * t52 = gm_l1;
                            uint64_t t53 = gm_l12;
                            gm_tl874t2atnlh3 t54 = gm_l4;
                            void * t55 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t51, t52, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t53, t54);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l14 = t55;
                        }
                        {
                            {
                                uint8_t t56 = (uint8_t)(((((uint8_t *)a2) + 8))[8] != 0);
                                if (t56) {
                                    goto gm_lb7;
                                }
                                gm_l23 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l23 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 438, 12);
                        }
gm_lb6:;
                        uint8_t t57 = gm_l23;
                        if (t57) {
                            void * t58 = gm_l5;
                            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_t88rcucg1b7uk t60 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t58, ((uint8_t)0), t59);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t61 = gm_l14;
                            gm_tao63pk3r18v5 t62;
                            GM_ST(uint8_t, (uint8_t *)&t62 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t62 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&t62 + 28, 0);
                            GM_ST(void *, ((uint8_t *)&t62 + 0), t61);
                            GM_ST(void *, ((uint8_t *)&t62 + 8), ((void *)0));
                            GM_ST(void *, ((uint8_t *)&t62 + 16), ((void *)0));
                            GM_ST(uint8_t, ((uint8_t *)&t62 + 24), ((uint8_t)0));
                            gm_tl874t2atnlh3 t63 = gm_l4;
                            gm_to0f5qn75cgg8 t64;
                            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t64 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t64 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t64 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t64 + 8), t62);
                            void * t65 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t60, &t64, t63);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l14 = t65;
                        }
                        {
                            void * t66 = gm_l0;
                            void * t67 = gm_l3;
                            uint64_t t68 = gm_l12;
                            gm_tl874t2atnlh3 t69 = gm_l4;
                            void * t70 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t66, t67, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l7)), t68, t69);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t70;
                        }
                        {
                            void * t71 = gm_l0;
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t71) + 2576), 40);
                        }
                        {
                            void * t72 = gm_l0;
                            gm_trqfvmjtt4sij t73;
                            gm_f_list_epvducg((void *)&t73, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t72) + 2576), t73);
                        }
                        void * t74 = gm_l0;
                        uint32_t t75 = gm_l6;
                        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                        gm_tnqk2qt7j42o9 t77;
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t77 + 0), t76);
                        gm_l17 = t77;
                        void * t78 = (void *)((uint8_t *)&gm_l17);
                        uint64_t t79 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t80;
                        GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t78 + (uintptr_t)((uint64_t)0ULL) * 4)));
                        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
                        gm_tl874t2atnlh3 t81 = gm_l4;
                        uint32_t t82 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t74, t75, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t82;
                        {
                            void * t83 = gm_l0;
                            memmove((((uint8_t *)t83) + 2576), ((uint8_t *)&F.l16), 40);
                        }
                        {
                            gm_trqfvmjtt4sij t84;
                            gm_f_list_vp1djv1_2((void *)&t84, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l19 = t84;
                        }
                        {
                            void * t85 = (void *)((uint8_t *)&F.l19);
                            void * t86 = gm_l14;
                            gm_tftb1odc739t5 t87 = gm_f_push_d943vgp_2(t85, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t86)));
                            gm_l24 = t87;
                        }
                        uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src4nj79mj9jk57, 448, 9);
                        {
                            void * t89 = (void *)((uint8_t *)&F.l19);
                            void * t90 = gm_l15;
                            gm_tftb1odc739t5 t91 = gm_f_push_d943vgp_2(t89, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t90)));
                            gm_l25 = t91;
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src4nj79mj9jk57, 449, 9);
                        {
                            void * t93 = (void *)((uint8_t *)&F.l9);
                            void * t94 = gm_l0;
                            void * t95 = GM_LD(void *, (((uint8_t *)t94) + 344));
                            uint64_t t96 = GM_LD(uint64_t, ((((uint8_t *)t94) + 344) + 8));
                            uint32_t t97 = gm_l18;
                            uint64_t t98 = (uint64_t)t97;
                            if ((uintptr_t)t98 >= (uintptr_t)(t96)) gm_trap_index((uintptr_t)t98, t96, gm_src4nj79mj9jk57, 450, 33);
                            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t95) + (uintptr_t)t98 * 136) + 56));
                            uint32_t t100 = gm_l18;
                            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                            gm_tsth9qu1dv0g3 t102;
                            memset(&t102, 0, sizeof t102);
                            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t102 + 8), t100);
                            gm_to76jli3bqddq t103;
                            GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t103 + 0), t102);
                            GM_ST(gm_b16_8, ((uint8_t *)&t103 + 40), t101);
                            gm_tl874t2atnlh3 t104 = gm_l4;
                            gm_to0f5qn75cgg8 t105;
                            GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)34ULL));
                            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t105 + 8), t103);
                            void * t106 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t99, &t105, t104);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_trcmmd12cefb1 t107;
                            GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t107 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t107 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)4ULL));
                            GM_ST(void *, ((uint8_t *)&t107 + 8), t106);
                            gm_tftb1odc739t5 t108 = gm_f_push_d6c76ud_2(t93, &t107);
                            gm_l26 = t108;
                        }
                        uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src4nj79mj9jk57, 450, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 436, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t110 = gm_l11;
            uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
            uint64_t t113 = r111;
            gm_l11 = t113;
        }
    }
gm_lb1:;
    {
        void * t114 = gm_l5;
        gm_t88rcucg1b7uk t115 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_tl874t2atnlh3 t117 = gm_l4;
        gm_tgabaikr08mjg t118;
        GM_ST(uint32_t, (uint8_t *)&t118 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t116);
        GM_ST(void *, ((uint8_t *)&t118 + 16), ((void *)0));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t118 + 24), t117);
        gm_tq7s98ihonkju t119 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t118, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6tu981lmau)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l27 = t119;
        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_src4nj79mj9jk57, 452, 38);
        void * t121 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
        gm_tl874t2atnlh3 t122 = gm_l4;
        gm_to0f5qn75cgg8 t123;
        memset(&t123, 0, sizeof t123);
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t123 + 8), t121);
        void * t124 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t115, &t123, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t124;
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
inline __attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_core_sd8eq62 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* f */
    gm_t3mf58gv9kf3n gm_l5; /* layouts */
    gm_t88rcucg1b7uk gm_l6; /* k */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    uint32_t gm_l9; /* inst */
    GM_STACK_CHECK(&F, 649u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l10;
    gm_b16_8 gm_l11;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss496b65r9omj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_b16_8 t4 = gm_l1;
    gm_t9805qtonbsag t5 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t5;
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
    if (t6) {
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
        gm_l4 = t7;
    } else {
        void * t8 = gm_l0;
        gm_tl874t2atnlh3 t9 = gm_l3;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sfflovsm8rlpo));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(50));
        gm_b0_1 t11 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_tn6p6lpnve7b0 *)gm_out), 0, sizeof (*(gm_tn6p6lpnve7b0 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t12 = gm_l0;
        gm_tl874t2atnlh3 t13 = gm_l3;
        gm_t3mf58gv9kf3n t14 = gm_f_soa_layout_n4ibc7b((void *)NULL, cx, NULL, t12, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t14;
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        gm_t88rcucg1b7uk t20 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t21 = gm_l0;
        gm_b16_8 t22 = gm_f_soa_fields_dg06fgc(t21, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l11 = t22;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
        uint64_t t24 = (uint64_t)t23;
        gm_tbci5ospk5nnn t25;
        gm_f_from_u64_ud13q5v((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t428ue2u3hf5g t26;
        gm_f_int_key_dmcraup((void *)&t26, (void *)NULL, cx, NULL, t17, t20, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)21ULL));
        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t27 + 8), t26);
        gm_t88rcucg1b7uk t28 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t16, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t28;
    }
    {
        void * t29 = gm_l0;
        memmove(((uint8_t *)&F.l7), (((uint8_t *)t29) + 2576), 40);
    }
    {
        void * t30 = gm_l0;
        gm_trqfvmjtt4sij t31;
        gm_f_list_epvducg((void *)&t31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t30) + 2576), t31);
    }
    void * t32 = gm_l0;
    uint32_t t33 = gm_l4;
    gm_t88rcucg1b7uk t34 = gm_l6;
    gm_tnqk2qt7j42o9 t35;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 0), t34);
    gm_l8 = t35;
    void * t36 = (void *)((uint8_t *)&gm_l8);
    uint64_t t37 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t36 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
    gm_tl874t2atnlh3 t39 = gm_l3;
    uint32_t t40 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t32, t33, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t40;
    {
        void * t41 = gm_l0;
        memmove((((uint8_t *)t41) + 2576), ((uint8_t *)&F.l7), 40);
    }
    {
        uint32_t t42 = gm_l9;
        void * t43 = GM_LD(void *, ((uint8_t *)&gm_l5));
        void * t44 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
        gm_t017ud7s0np4m t45;
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t42);
        GM_ST(void *, ((uint8_t *)&t45 + 8), t43);
        GM_ST(void *, ((uint8_t *)&t45 + 16), t44);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 28, 0);
        GM_ST(gm_t017ud7s0np4m, ((uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out)), t45);
        ((uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out))[24] = 1;
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
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_soa_expand_2e8rmqf F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    void * gm_l2; /* e */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tandvbt63uk7n gm_l4; /* x */
    gm_tesaltf0mhcki gm_l5; /* op */
    gm_b16_8 gm_l6; /* args */
    void * gm_l7; /* err */
    gm_t88rcucg1b7uk gm_l8; /* operand */
    gm_t88rcucg1b7uk gm_l9; /* ok */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_b16_8 gm_l11; /* stmts */
    gm_toh0ootodhjue gm_l12; /* b */
    gm_tjpqvfd72o62u gm_l13; /* to */
    uint64_t gm_l14; /* next */
    void * gm_l15; /* lo */
    void * gm_l16; /* hi */
    GM_STACK_CHECK(&F, 1649u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l17;
    gm_t8cc1mtbrhlpt gm_l18;
    gm_tq7s98ihonkju gm_l19;
    gm_t8cc1mtbrhlpt gm_l20;
    {
        void * t15 = gm_l2;
        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 72));
        gm_l3 = t16;
        {
            {
                {
                    {
                        void * t17 = gm_l2;
                        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
                        uint8_t t19 = (uint8_t)(t18 == ((uint8_t)58ULL));
                        if (!t19) {
                            goto gm_lb2;
                        }
                        memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t17) + 8) + 8), 24);
                        gm_tesaltf0mhcki t20 = GM_LD(gm_tesaltf0mhcki, ((uint8_t *)&gm_l4));
                        gm_l5 = t20;
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                        gm_l6 = t21;
                        void * t22 = gm_l0;
                        void * t23 = (void *)(((uint8_t *)t22) + 8);
                        gm_t88rcucg1b7uk t24 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t23);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t25 = gm_f_empty_slice_g3tddrb_2();
                        gm_tl874t2atnlh3 t26 = gm_l3;
                        gm_to0f5qn75cgg8 t27;
                        memset(&t27, 0, sizeof t27);
                        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)35ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 8), t25);
                        void * t28 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t24, &t27, t26);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l7 = t28;
                        void * t29 = gm_l0;
                        void * t30 = (void *)(((uint8_t *)t29) + 8);
                        gm_t88rcucg1b7uk t31 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t31;
                        gm_tesaltf0mhcki t32 = gm_l5;
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t32) + 0));
                        uint8_t t34 = (uint8_t)((t33 == ((uint8_t)5ULL)));
                        gm_tesaltf0mhcki t35;
                        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)5ULL));
                        if (t34) {
                            {
                                {
                                    void * t36 = gm_l0;
                                    void * t37 = (void *)(((uint8_t *)t36) + 8);
                                    void * t38 = gm_l2;
                                    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t38));
                                    gm_f_kind_jcnt4hm((void *)&gm_l17, t37, GM_CONV(gm_csguankko4lmhd, t39));
                                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
                                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)14ULL));
                                    if (!t41) {
                                        goto gm_lb4;
                                    }
                                    gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l17) + 8) + 4));
                                    gm_l9 = t42;
                                    gm_t88rcucg1b7uk t43 = gm_l9;
                                    gm_l8 = t43;
                                    goto gm_lb3;
                                }
gm_lb4:;
                            }
gm_lb3:;
                        } else {
                            void * t44 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t45, gm_src4nj79mj9jk57, 468, 19);
                            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + (uintptr_t)((uint64_t)0ULL) * 88));
                            gm_l8 = t46;
                        }
                        void * t47 = gm_l0;
                        gm_t88rcucg1b7uk t48 = gm_l8;
                        gm_f_soa_info_pq4q43l((void *)&gm_l18, t47, GM_CONV(gm_csguankko4lmhd, t48));
                        uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l18))[32] != 0);
                        if (t49) {
                            memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l18), 32);
                        } else {
                            void * t50 = gm_l7;
                            F.ret = t50;
                            goto gm_fret;
                        }
                        void * t51 = gm_l0;
                        uint8_t t52 = gm_f_soa_generic_kvk21mq(t51, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)));
                        if (t52) {
                            void * t53 = gm_l7;
                            F.ret = t53;
                            goto gm_fret;
                        }
                        {
                            {
                                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                                if (!t55) {
                                    goto gm_lb6;
                                }
                                void * t56 = gm_l0;
                                void * t57 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t58, gm_src4nj79mj9jk57, 473, 41);
                                void * t59 = (void *)(((uint8_t *)t57) + (uintptr_t)((uint64_t)0ULL) * 88);
                                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t61, gm_src4nj79mj9jk57, 473, 57);
                                void * t62 = (void *)(((uint8_t *)t60) + (uintptr_t)((uint64_t)1ULL) * 88);
                                gm_tl874t2atnlh3 t63 = gm_l3;
                                void * t64 = gm_f_soa_gather_of_4timqsa((void *)NULL, cx, NULL, t56, t59, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t62, t63);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t64;
                                goto gm_fret;
                            }
gm_lb6:;
                            {
                                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)1ULL));
                                if (!t66) {
                                    goto gm_lb7;
                                }
                                {
                                    void * t67 = gm_l0;
                                    void * t68 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t69)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t69, gm_src4nj79mj9jk57, 475, 37);
                                    void * t70 = (void *)(((uint8_t *)t68) + (uintptr_t)((uint64_t)0ULL) * 88);
                                    void * t71 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t72, gm_src4nj79mj9jk57, 475, 53);
                                    void * t73 = (void *)(((uint8_t *)t71) + (uintptr_t)((uint64_t)1ULL) * 88);
                                    void * t74 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t75)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t75, gm_src4nj79mj9jk57, 475, 63);
                                    void * t76 = (void *)(((uint8_t *)t74) + (uintptr_t)((uint64_t)2ULL) * 88);
                                    gm_tl874t2atnlh3 t77 = gm_l3;
                                    gm_b16_8 t78 = gm_f_soa_scatter_h5ruqu8((void *)NULL, cx, NULL, t67, t70, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t73, t76, t77);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l11 = t78;
                                }
                                {
                                    void * t79 = gm_l0;
                                    void * t80 = (void *)(((uint8_t *)t79) + 8);
                                    gm_t88rcucg1b7uk t81 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t82 = gm_l11;
                                    gm_tl874t2atnlh3 t83 = gm_l3;
                                    gm_tgabaikr08mjg t84;
                                    GM_ST(uint32_t, (uint8_t *)&t84 + 36, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t82);
                                    GM_ST(void *, ((uint8_t *)&t84 + 16), ((void *)0));
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t84 + 24), t83);
                                    gm_tq7s98ihonkju t85 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t84, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2ok1rai48t)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l19 = t85;
                                    uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                    if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src4nj79mj9jk57, 476, 48);
                                    void * t87 = GM_LD(void *, (((uint8_t *)&gm_l19) + 8));
                                    gm_tl874t2atnlh3 t88 = gm_l3;
                                    gm_to0f5qn75cgg8 t89;
                                    memset(&t89, 0, sizeof t89);
                                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)46ULL));
                                    GM_ST(void *, ((uint8_t *)&t89 + 8), t87);
                                    void * t90 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t81, &t89, t88);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t90;
                                    goto gm_fret;
                                }
                            }
gm_lb7:;
                            {
                                uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t92 = (uint8_t)(t91 == ((uint8_t)2ULL));
                                if (!t92) {
                                    goto gm_lb8;
                                }
                                void * t93 = gm_l0;
                                void * t94 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t95, gm_src4nj79mj9jk57, 478, 38);
                                void * t96 = (void *)(((uint8_t *)t94) + (uintptr_t)((uint64_t)0ULL) * 88);
                                gm_tl874t2atnlh3 t97 = gm_l3;
                                void * t98 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t93, t96, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t97);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t98;
                                goto gm_fret;
                            }
gm_lb8:;
                            {
                                uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t100 = (uint8_t)(t99 == ((uint8_t)3ULL));
                                if (!t100) {
                                    goto gm_lb9;
                                }
                                gm_toh0ootodhjue t101 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l5) + 1));
                                gm_l12 = t101;
                                {
                                    void * t102 = gm_l0;
                                    void * t103 = gm_l2;
                                    gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t103));
                                    gm_f_soa_info_pq4q43l((void *)&gm_l20, t102, GM_CONV(gm_csguankko4lmhd, t104));
                                }
                                uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l20))[32] != 0);
                                if (t105) {
                                    memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l20), 32);
                                } else {
                                    void * t106 = gm_l7;
                                    F.ret = t106;
                                    goto gm_fret;
                                }
                                {
                                    void * t107 = gm_l0;
                                    uint8_t t108 = gm_f_soa_generic_kvk21mq(t107, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l13)));
                                    if (t108) {
                                        void * t109 = gm_l7;
                                        F.ret = t109;
                                        goto gm_fret;
                                    }
                                }
                                gm_l14 = ((uint64_t)1ULL);
                                gm_l15 = ((void *)0);
                                gm_l16 = ((void *)0);
                                uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)&gm_l12));
                                if (t110) {
                                    void * t111 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t113 = gm_l14;
                                    if ((uintptr_t)t113 >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)t113, t112, gm_src4nj79mj9jk57, 486, 23);
                                    void * t114 = (void *)(((uint8_t *)t111) + (uintptr_t)t113 * 88);
                                    gm_l15 = t114;
                                    uint64_t t115 = gm_l14;
                                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                                    if (o117) gm_trap("integer overflow in `+`", gm_src4nj79mj9jk57, 487, 17);
                                    uint64_t t118 = r116;
                                    gm_l14 = t118;
                                }
                                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 1));
                                if (t119) {
                                    void * t120 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t122 = gm_l14;
                                    if ((uintptr_t)t122 >= (uintptr_t)(t121)) gm_trap_index((uintptr_t)t122, t121, gm_src4nj79mj9jk57, 489, 29);
                                    void * t123 = (void *)(((uint8_t *)t120) + (uintptr_t)t122 * 88);
                                    gm_l16 = t123;
                                }
                                {
                                    void * t124 = gm_l0;
                                    void * t125 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t126)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t126, gm_src4nj79mj9jk57, 490, 41);
                                    void * t127 = (void *)(((uint8_t *)t125) + (uintptr_t)((uint64_t)0ULL) * 88);
                                    uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
                                    void * t129 = gm_l15;
                                    void * t130 = gm_l16;
                                    uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 2));
                                    gm_tl874t2atnlh3 t132 = gm_l3;
                                    void * t133 = gm_f_soa_slice_columns_kqq8dfr((void *)NULL, cx, NULL, t124, t127, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t128, t129, t130, t131, t132);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t133;
                                    goto gm_fret;
                                }
                            }
gm_lb9:;
                            {
                                uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t135 = (uint8_t)(t134 == ((uint8_t)4ULL));
                                if (!t135) {
                                    goto gm_lb10;
                                }
                                void * t136 = gm_l0;
                                void * t137 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t138, gm_src4nj79mj9jk57, 492, 44);
                                void * t139 = (void *)(((uint8_t *)t137) + (uintptr_t)((uint64_t)0ULL) * 88);
                                void * t140 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t141, gm_src4nj79mj9jk57, 492, 53);
                                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t140) + (uintptr_t)((uint64_t)0ULL) * 88));
                                void * t143 = gm_l2;
                                gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t143));
                                gm_tl874t2atnlh3 t145 = gm_l3;
                                void * t146 = gm_f_soa_to_const_hhiu49k((void *)NULL, cx, NULL, t136, t139, t142, t144, t145);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t146;
                                goto gm_fret;
                            }
gm_lb10:;
                            {
                                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t148 = (uint8_t)(t147 == ((uint8_t)5ULL));
                                if (!t148) {
                                    goto gm_lb11;
                                }
                                void * t149 = gm_l0;
                                void * t150 = gm_l1;
                                gm_t88rcucg1b7uk t151 = gm_l8;
                                void * t152 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t153)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t153, gm_src4nj79mj9jk57, 493, 68);
                                void * t154 = (void *)(((uint8_t *)t152) + (uintptr_t)((uint64_t)0ULL) * 88);
                                void * t155 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t156)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t156, gm_src4nj79mj9jk57, 493, 78);
                                void * t157 = (void *)(((uint8_t *)t155) + (uintptr_t)((uint64_t)1ULL) * 88);
                                gm_tl874t2atnlh3 t158 = gm_l3;
                                void * t159 = gm_f_soa_make_columns_ardr0ua((void *)NULL, cx, NULL, t149, t150, t151, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t154, t157, t158);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t159;
                                goto gm_fret;
                            }
gm_lb11:;
                            {
                                uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t161 = (uint8_t)(t160 == ((uint8_t)6ULL));
                                if (!t161) {
                                    goto gm_lb12;
                                }
                                void * t162 = gm_l0;
                                void * t163 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t164)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t164, gm_src4nj79mj9jk57, 494, 45);
                                void * t165 = (void *)(((uint8_t *)t163) + (uintptr_t)((uint64_t)0ULL) * 88);
                                void * t166 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t167)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t167, gm_src4nj79mj9jk57, 494, 61);
                                void * t168 = (void *)(((uint8_t *)t166) + (uintptr_t)((uint64_t)1ULL) * 88);
                                gm_tl874t2atnlh3 t169 = gm_l3;
                                void * t170 = gm_f_soa_free_columns_n1tvn1j((void *)NULL, cx, NULL, t162, t165, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t168, t169);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t170;
                                goto gm_fret;
                            }
gm_lb12:;
                            {
                                uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t172 = (uint8_t)(t171 == ((uint8_t)7ULL));
                                if (!t172) {
                                    goto gm_lb13;
                                }
                                void * t173 = gm_l0;
                                void * t174 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t175)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t175, gm_src4nj79mj9jk57, 495, 49);
                                void * t176 = (void *)(((uint8_t *)t174) + (uintptr_t)((uint64_t)0ULL) * 88);
                                void * t177 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t178)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t178, gm_src4nj79mj9jk57, 495, 65);
                                void * t179 = (void *)(((uint8_t *)t177) + (uintptr_t)((uint64_t)1ULL) * 88);
                                gm_tl874t2atnlh3 t180 = gm_l3;
                                void * t181 = gm_f_soa_copy_columns_s4nhe5h((void *)NULL, cx, NULL, t173, t176, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t179, t180);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t181;
                                goto gm_fret;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 472, 5);
                        }
gm_lb5:;
                    }
                }
gm_lb2:;
            }
            {
                void * t182 = gm_l2;
                F.ret = t182;
                goto gm_fret;
            }
        }
gm_lb1:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_fields_dg06fgc(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_free_columns_n1tvn1j F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* site */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t017ud7s0np4m gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* first */
    void * gm_l8; /* base */
    void * gm_l9; /* raw */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 1243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tn6p6lpnve7b0 gm_l12;
    gm_b16_8 gm_l13;
    gm_th2lqbo77pvsc gm_l14;
    gm_b16_8 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_l5 = t3;
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sg7n1u5l63uaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(8));
        gm_tl874t2atnlh3 t6 = gm_l4;
        gm_tjpqvfd72o62u t7 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a2));
        gm_f_soa_core_sd8eq62((void *)&gm_l12, (void *)NULL, cx, NULL, t4, t5, &t7, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l12))[24] != 0);
    if (t8) {
        memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l12), 24);
    } else {
        void * t9 = gm_l5;
        gm_t88rcucg1b7uk t10 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
        gm_tl874t2atnlh3 t12 = gm_l4;
        gm_to0f5qn75cgg8 t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t11);
        void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t13, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t14;
        goto gm_fret;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l1;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t16));
        gm_f_struct_fields_chqu3it((void *)&gm_l14, t15, GM_CONV(gm_csguankko4lmhd, t17));
    }
    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
    if (t18) {
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l13 = t19;
    } else {
        gm_b16_8 t20 = gm_f_empty_slice_ose1ba9();
        gm_l13 = t20;
    }
    gm_b16_8 t21 = gm_l13;
    gm_l15 = t21;
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l15));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t23, gm_src4nj79mj9jk57, 412, 14);
    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t22) + (uintptr_t)((uint64_t)0ULL) * 40) + 16));
    gm_l7 = t24;
    {
        gm_t88rcucg1b7uk t25 = gm_l7;
        void * t26 = gm_l1;
        void * t27 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbl8il92l97ut t28;
        GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t28 + 0), t27);
        GM_ST(uint32_t, ((uint8_t *)&t28 + 8), ((uint32_t)0ULL));
        gm_tl874t2atnlh3 t29 = gm_l4;
        gm_to0f5qn75cgg8 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t30 + 8), t28);
        void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t25, &t30, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t31;
    }
    {
        void * t32 = gm_l5;
        gm_t53at80pfdus3 t33;
        memset(&t33, 0, sizeof t33);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t34 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t32, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t35 = gm_l8;
        gm_tmn7n0n9raou5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)6ULL));
        gm_t8jsc4n9vp2ib t37;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t37 + 0), t36);
        GM_ST(void *, ((uint8_t *)&t37 + 8), t35);
        gm_tl874t2atnlh3 t38 = gm_l4;
        gm_to0f5qn75cgg8 t39;
        memset(&t39, 0, sizeof t39);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)32ULL));
        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t39 + 8), t37);
        void * t40 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t39, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t40;
    }
    {
        void * t41 = gm_l0;
        void * t42 = gm_l1;
        gm_tl874t2atnlh3 t43 = gm_l4;
        void * t44 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t41, t42, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t44;
    }
    {
        gm_trqfvmjtt4sij t45;
        gm_f_list_vp1djv1_2((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t45;
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l11);
        void * t47 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
        gm_tftb1odc739t5 t48 = gm_f_push_d943vgp_2(t46, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t47)));
        gm_l16 = t48;
    }
    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src4nj79mj9jk57, 417, 5);
    {
        void * t50 = (void *)((uint8_t *)&F.l11);
        void * t51 = GM_LD(void *, (((uint8_t *)&gm_l6) + 16));
        gm_tftb1odc739t5 t52 = gm_f_push_d943vgp_2(t50, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t51)));
        gm_l17 = t52;
    }
    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src4nj79mj9jk57, 418, 5);
    {
        void * t54 = (void *)((uint8_t *)&F.l11);
        void * t55 = gm_l10;
        gm_tftb1odc739t5 t56 = gm_f_push_d943vgp_2(t54, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t55)));
        gm_l18 = t56;
    }
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src4nj79mj9jk57, 419, 5);
    {
        void * t58 = (void *)((uint8_t *)&F.l11);
        void * t59 = gm_l9;
        gm_tftb1odc739t5 t60 = gm_f_push_d943vgp_2(t58, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t59)));
        gm_l19 = t60;
    }
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src4nj79mj9jk57, 420, 5);
    {
        void * t62 = (void *)((uint8_t *)&F.l11);
        void * t63 = gm_l3;
        gm_tftb1odc739t5 t64 = gm_f_push_d943vgp_2(t62, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t63)));
        gm_l20 = t64;
    }
    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src4nj79mj9jk57, 421, 5);
    {
        void * t66 = gm_l0;
        void * t67 = GM_LD(void *, (((uint8_t *)t66) + 344));
        uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t66) + 344) + 8));
        uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
        uint64_t t70 = (uint64_t)t69;
        if ((uintptr_t)t70 >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)t70, t68, gm_src4nj79mj9jk57, 422, 19);
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t67) + (uintptr_t)t70 * 136) + 56));
        uint32_t t72 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_tsth9qu1dv0g3 t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t74 + 8), t72);
        gm_to76jli3bqddq t75;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t75 + 0), t74);
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 40), t73);
        gm_tl874t2atnlh3 t76 = gm_l4;
        gm_to0f5qn75cgg8 t77;
        GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t77 + 8), t75);
        void * t78 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t71, &t77, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t78;
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
inline __attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_gather_of_4timqsa F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    void * gm_l11; /* col */
    GM_STACK_CHECK(&F, 678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t8ta482bl6be0 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_f_soa_fields_dg06fgc(t7, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l5 = t8;
    }
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_vp1djv1_2((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t9;
    }
    gm_b16_8 t10 = gm_l5;
    gm_l7 = t10;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l8;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t14 = gm_l8;
                    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t17 = gm_l8;
                    gm_t0mglksrkfa5e t18 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t15) + (uintptr_t)t17 * 40));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l12 + 0), t14);
                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l12 + 8), t18);
                }
                {
                    {
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                        gm_l9 = t19;
                        {
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l12) + 8), 40);
                        }
                        {
                            void * t20 = gm_l0;
                            void * t21 = gm_l1;
                            uint64_t t22 = gm_l9;
                            gm_tl874t2atnlh3 t23 = gm_l4;
                            void * t24 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t20, t21, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t22, t23);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l11 = t24;
                        }
                        {
                            void * t25 = (void *)((uint8_t *)&F.l6);
                            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                            void * t27 = gm_l11;
                            void * t28 = gm_l3;
                            void * t29 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t28);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tlcl4u558l6p5 t30;
                            GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
                            GM_ST(void *, ((uint8_t *)&t30 + 0), t27);
                            GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
                            GM_ST(uint8_t, ((uint8_t *)&t30 + 16), ((uint8_t)1));
                            gm_tl874t2atnlh3 t31 = gm_l4;
                            gm_to0f5qn75cgg8 t32;
                            memset(&t32, 0, sizeof t32);
                            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t32 + 8), t30);
                            void * t33 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t26, &t32, t31);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t34 = gm_f_push_d943vgp_2(t25, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t33)));
                            gm_l13 = t34;
                        }
                        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src4nj79mj9jk57, 280, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 278, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t36 = gm_l8;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l8 = t39;
        }
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)a2));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_tl874t2atnlh3 t42 = gm_l4;
        gm_to0f5qn75cgg8 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 8), t41);
        void * t44 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t43, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t44;
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
extern __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_generic_kvk21mq(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_info_pq4q43l F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l6; /* tm */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    GM_STACK_CHECK(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l8;
    {
        {
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 8);
                gm_t88rcucg1b7uk t13 = gm_l1;
                gm_f_kind_jcnt4hm((void *)&gm_l8, t12, GM_CONV(gm_csguankko4lmhd, t13));
            }
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)17ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 8));
            gm_l2 = t16;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l8) + 8) + 8));
            gm_l3 = t17;
            void * t18 = gm_l0;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 2536));
            gm_l4 = t19;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t22 = (t20 < t21);
                    if (!t22) {
                        goto gm_lb3;
                    }
                    {
                        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t25 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)t23) + (uintptr_t)t25 * 32), 32);
                                uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                uint32_t t27 = gm_l2;
                                uint8_t t28 = ((t26 == t27));
                                if (t28) {
                                    {
                                        {
                                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                            uint8_t t30 = (t29 > ((uint64_t)0ULL));
                                            if (!t30) {
                                                goto gm_lb9;
                                            }
                                            void * t31 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t31) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                                            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                            if (!t34) {
                                                goto gm_lb9;
                                            }
                                            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t31) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                                            gm_l7 = t35;
                                            {
                                                gm_t88rcucg1b7uk t36 = gm_l7;
                                                gm_tf5srh4mp4b3i t37 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l6) + 8));
                                                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                                                gm_tjpqvfd72o62u t39;
                                                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                                                GM_ST(uint8_t, (uint8_t *)&t39 + 25, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t39 + 26, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t39 + 28, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 0), t36);
                                                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t39 + 8), t37);
                                                GM_ST(uint8_t, ((uint8_t *)&t39 + 24), t38);
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 33, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 34, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 36, 0);
                                                GM_ST(gm_tjpqvfd72o62u, ((uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out)), t39);
                                                ((uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out))[32] = 1;
                                                goto gm_fdone;
                                            }
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    memset(&(*(gm_t8cc1mtbrhlpt *)gm_out), 0, sizeof (*(gm_t8cc1mtbrhlpt *)gm_out));
                                    goto gm_fdone;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 72, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_lb3:;
            {
                void * t44 = gm_l0;
                void * t45 = (void *)(((uint8_t *)t44) + 2440);
                uint32_t t46 = gm_l2;
                gm_f_get_nt3g9ko((void *)&(*(gm_t8cc1mtbrhlpt *)gm_out), t45, t46);
                goto gm_fdone;
            }
        }
gm_lb2:;
        memset(&(*(gm_t8cc1mtbrhlpt *)gm_out), 0, sizeof (*(gm_t8cc1mtbrhlpt *)gm_out));
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
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1) { gm_t8cc1mtbrhlpt gm_r; gm_f_soa_info_pq4q43l(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_layout_n4ibc7b F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tbccvikg6b96e gm_l9; /* l */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_f_soa_fields_dg06fgc(t7, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a1)));
        gm_l3 = t8;
    }
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_vp1djv1_2((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t9;
    }
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_vp1djv1_2((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t10;
    }
    gm_b16_8 t11 = gm_l3;
    gm_l6 = t11;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l7;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t17 = gm_l7;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t15) + (uintptr_t)t17 * 40), 40);
                        }
                        {
                            void * t18 = gm_l0;
                            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                            gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t18, t19);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t20 = (void *)((uint8_t *)&F.l4);
                            void * t21 = gm_l0;
                            uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                            gm_tl874t2atnlh3 t23 = gm_l2;
                            void * t24 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t21, t22, t23);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t25 = gm_f_push_d943vgp_2(t20, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t24)));
                            gm_l12 = t25;
                        }
                        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src4nj79mj9jk57, 362, 9);
                        {
                            void * t27 = (void *)((uint8_t *)&F.l5);
                            void * t28 = gm_l0;
                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            gm_tl874t2atnlh3 t30 = gm_l2;
                            void * t31 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t28, t29, t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t32 = gm_f_push_d943vgp_2(t27, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t31)));
                            gm_l13 = t32;
                        }
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src4nj79mj9jk57, 363, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 360, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l7;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l7 = t37;
        }
    }
gm_lb1:;
    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t39 = (uint64_t)t38;
    gm_l10 = t39;
    void * t40 = gm_l0;
    void * t41 = (void *)(((uint8_t *)t40) + 8);
    uint64_t t42 = gm_l10;
    void * t43 = gm_l0;
    void * t44 = (void *)(((uint8_t *)t43) + 8);
    gm_t88rcucg1b7uk t45 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t44);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t46 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t41, t42, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t46;
    {
        gm_t88rcucg1b7uk t47 = gm_l11;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_tl874t2atnlh3 t49 = gm_l2;
        gm_to0f5qn75cgg8 t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t48);
        void * t51 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t47, &t50, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t52 = gm_l11;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_tl874t2atnlh3 t54 = gm_l2;
        gm_to0f5qn75cgg8 t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 8), t53);
        void * t56 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t52, &t55, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t57;
        GM_ST(void *, ((uint8_t *)&t57 + 0), t51);
        GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
        F.ret = t57;
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
inline __attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_len_of_4l5vk85 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* n */
    gm_b16_8 gm_l5; /* fields */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l7;
    {
        {
            uint8_t t4 = (uint8_t)(((((uint8_t *)a2) + 8))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
            gm_l4 = t5;
            void * t6 = gm_l0;
            uint64_t t7 = gm_l4;
            gm_tl874t2atnlh3 t8 = gm_l3;
            void * t9 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t11));
        gm_f_struct_fields_chqu3it((void *)&gm_l7, t10, GM_CONV(gm_csguankko4lmhd, t12));
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
    if (t13) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l5 = t14;
    } else {
        gm_b16_8 t15 = gm_f_empty_slice_ose1ba9();
        gm_l5 = t15;
    }
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
    if (t17) {
        gm_l6 = ((uint64_t)0ULL);
    } else {
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 270, 42);
        uint64_t t21 = r19;
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        gm_t88rcucg1b7uk t24 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = gm_l1;
        void * t26 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t27 = gm_l6;
        uint32_t t28 = (uint32_t)t27;
        gm_tbl8il92l97ut t29;
        GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t29 + 0), t26);
        GM_ST(uint32_t, ((uint8_t *)&t29 + 8), t28);
        gm_tl874t2atnlh3 t30 = gm_l3;
        gm_to0f5qn75cgg8 t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t31 + 8), t29);
        void * t32 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t24, &t31, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t32;
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
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_soa_make_columns_ardr0ua F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tjpqvfd72o62u gm_l3; /* info */
    void * gm_l4; /* n */
    void * gm_l5; /* site */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* tys */
    gm_t017ud7s0np4m gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* rt */
    void * gm_l11; /* call */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint32_t gm_l14; /* p */
    uint32_t gm_l15; /* e */
    gm_b16_8 gm_l16; /* fields */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    void * gm_l21; /* raw */
    gm_t88rcucg1b7uk gm_l22; /* result */
    void * gm_l23; /* value */
    void * gm_l24; /* wrapped */
    void * gm_l25; /* failed */
    GM_STACK_CHECK(&F, 3017u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tn6p6lpnve7b0 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_t53at80pfdus3 gm_l32;
    gm_th2lqbo77pvsc gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_l7 = t10;
    {
        void * t11 = gm_l0;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_suh0878eve8t9));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(9));
        gm_tl874t2atnlh3 t13 = gm_l6;
        gm_tjpqvfd72o62u t14 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a3));
        gm_f_soa_core_sd8eq62((void *)&gm_l27, (void *)NULL, cx, NULL, t11, t12, &t14, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l27))[24] != 0);
    if (t15) {
        memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l27), 24);
    } else {
        void * t16 = gm_l7;
        gm_t88rcucg1b7uk t17 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t18 = gm_f_empty_slice_g3tddrb_2();
        gm_tl874t2atnlh3 t19 = gm_l6;
        gm_to0f5qn75cgg8 t20;
        memset(&t20, 0, sizeof t20);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t18);
        void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t20, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t21;
        goto gm_fret;
    }
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, (((uint8_t *)t22) + 344));
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 344) + 8));
    uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
    uint64_t t26 = (uint64_t)t25;
    if ((uintptr_t)t26 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t26, t24, gm_src4nj79mj9jk57, 374, 11);
    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t23) + (uintptr_t)t26 * 136) + 56));
    gm_l9 = t27;
    {
        gm_trqfvmjtt4sij t28;
        gm_f_list_vp1djv1_2((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t28;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l10);
        void * t30 = GM_LD(void *, (((uint8_t *)&gm_l8) + 8));
        gm_tftb1odc739t5 t31 = gm_f_push_d943vgp_2(t29, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t30)));
        gm_l28 = t31;
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src4nj79mj9jk57, 376, 5);
    {
        void * t33 = (void *)((uint8_t *)&F.l10);
        void * t34 = GM_LD(void *, (((uint8_t *)&gm_l8) + 16));
        gm_tftb1odc739t5 t35 = gm_f_push_d943vgp_2(t33, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t34)));
        gm_l29 = t35;
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src4nj79mj9jk57, 377, 5);
    {
        void * t37 = (void *)((uint8_t *)&F.l10);
        void * t38 = gm_l4;
        void * t39 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t40 = gm_f_push_d943vgp_2(t37, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t39)));
        gm_l30 = t40;
    }
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src4nj79mj9jk57, 378, 5);
    {
        void * t42 = (void *)((uint8_t *)&F.l10);
        void * t43 = gm_l5;
        gm_tftb1odc739t5 t44 = gm_f_push_d943vgp_2(t42, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t43)));
        gm_l31 = t44;
    }
    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src4nj79mj9jk57, 379, 5);
    {
        gm_t88rcucg1b7uk t46 = gm_l9;
        uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_tsth9qu1dv0g3 t49;
        memset(&t49, 0, sizeof t49);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t49 + 8), t47);
        gm_to76jli3bqddq t50;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t50 + 0), t49);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 40), t48);
        gm_tl874t2atnlh3 t51 = gm_l6;
        gm_to0f5qn75cgg8 t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t52 + 8), t50);
        void * t53 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t46, &t52, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t53;
    }
    {
        {
            {
                void * t54 = gm_l7;
                gm_t88rcucg1b7uk t55 = gm_l9;
                gm_f_kind_jcnt4hm((void *)&gm_l32, t54, GM_CONV(gm_csguankko4lmhd, t55));
            }
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)14ULL));
            if (!t57) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l32) + 8));
            gm_l12 = t58;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l32) + 8) + 4));
            gm_l13 = t59;
            void * t60 = gm_l1;
            gm_t88rcucg1b7uk t61 = gm_l13;
            gm_tl874t2atnlh3 t62 = gm_l6;
            uint32_t t63 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t63;
            void * t64 = gm_l1;
            gm_t88rcucg1b7uk t65 = gm_l12;
            gm_tl874t2atnlh3 t66 = gm_l6;
            uint32_t t67 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t64, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t67;
            {
                void * t68 = gm_l0;
                gm_t88rcucg1b7uk t69 = gm_l2;
                gm_f_struct_fields_chqu3it((void *)&gm_l33, t68, GM_CONV(gm_csguankko4lmhd, t69));
            }
            uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l33))[16] != 0);
            if (t70) {
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l33));
                gm_l16 = t71;
            } else {
                gm_b16_8 t72 = gm_f_empty_slice_ose1ba9();
                gm_l16 = t72;
            }
            {
                gm_trqfvmjtt4sij t73;
                gm_f_list_vp1djv1_2((void *)&t73, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l17 = t73;
            }
            gm_l19 = ((uint64_t)0ULL);
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
            uint64_t r75; bool o76 = __builtin_sub_overflow(t74, ((uint64_t)1ULL), &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 387, 17);
            uint64_t t77 = r75;
            gm_l18 = t77;
            {
                for (;;) {
                    uint64_t t78 = gm_l19;
                    uint64_t t79 = gm_l18;
                    uint8_t t80 = (t78 < t79);
                    if (!t80) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t81 = gm_l19;
                                gm_l20 = t81;
                                {
                                    void * t82 = gm_l7;
                                    gm_t53at80pfdus3 t83;
                                    memset(&t83, 0, sizeof t83);
                                    GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)7ULL));
                                    gm_t88rcucg1b7uk t84 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t82, &t83);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t88rcucg1b7uk t85 = gm_l13;
                                    uint32_t t86 = gm_l14;
                                    gm_tl874t2atnlh3 t87 = gm_l6;
                                    gm_to0f5qn75cgg8 t88;
                                    memset(&t88, 0, sizeof t88);
                                    GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)15ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t88 + 8), t86);
                                    void * t89 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t85, &t88, t87);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t90 = gm_l0;
                                    uint64_t t91 = gm_l20;
                                    uint64_t t92 = (uint64_t)t91;
                                    gm_tl874t2atnlh3 t93 = gm_l6;
                                    void * t94 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t90, t92, t93);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tlcl4u558l6p5 t95;
                                    GM_ST(uint8_t, (uint8_t *)&t95 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t95 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t95 + 20, 0);
                                    GM_ST(void *, ((uint8_t *)&t95 + 0), t89);
                                    GM_ST(void *, ((uint8_t *)&t95 + 8), t94);
                                    GM_ST(uint8_t, ((uint8_t *)&t95 + 16), ((uint8_t)0));
                                    gm_tl874t2atnlh3 t96 = gm_l6;
                                    gm_to0f5qn75cgg8 t97;
                                    memset(&t97, 0, sizeof t97);
                                    GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)20ULL));
                                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t97 + 8), t95);
                                    void * t98 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t84, &t97, t96);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l21 = t98;
                                }
                                {
                                    void * t99 = (void *)((uint8_t *)&F.l17);
                                    void * t100 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint64_t t102 = gm_l20;
                                    if ((uintptr_t)t102 >= (uintptr_t)(t101)) gm_trap_index((uintptr_t)t102, t101, gm_src4nj79mj9jk57, 389, 27);
                                    gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t100) + (uintptr_t)t102 * 40) + 16));
                                    void * t104 = gm_l21;
                                    gm_t8588ma2cr9d7 t105;
                                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t105 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)8ULL));
                                    gm_b16_8 t106 = gm_f_one_arg_f1gj3pp((void *)NULL, cx, NULL, t104);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t9q8lhljes74c t107;
                                    GM_ST(uint32_t, (uint8_t *)&t107 + 12, 0);
                                    GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t107 + 0), t105);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 16), t106);
                                    gm_tl874t2atnlh3 t108 = gm_l6;
                                    gm_to0f5qn75cgg8 t109;
                                    GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t109 + 40, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t109 + 48, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t109 + 56, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)33ULL));
                                    GM_ST(gm_t9q8lhljes74c, ((uint8_t *)&t109 + 8), t107);
                                    void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t103, &t109, t108);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t111 = gm_f_push_d943vgp_2(t99, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t110)));
                                    gm_l34 = t111;
                                }
                                uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src4nj79mj9jk57, 389, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 387, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t113 = gm_l19;
                    uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                    uint64_t t116 = r114;
                    gm_l19 = t116;
                }
            }
gm_lb3:;
            {
                void * t117 = (void *)((uint8_t *)&F.l17);
                void * t118 = gm_l4;
                gm_tftb1odc739t5 t119 = gm_f_push_d943vgp_2(t117, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t118)));
                gm_l35 = t119;
            }
            uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src4nj79mj9jk57, 391, 5);
            void * t121 = gm_l7;
            gm_t88rcucg1b7uk t122 = gm_l12;
            gm_t88rcucg1b7uk t123 = gm_l2;
            gm_t88rcucg1b7uk t124 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t121, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t124;
            {
                gm_t88rcucg1b7uk t125 = gm_l2;
                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                gm_tl874t2atnlh3 t127 = gm_l6;
                gm_to0f5qn75cgg8 t128;
                memset(&t128, 0, sizeof t128);
                GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)35ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t128 + 8), t126);
                void * t129 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t125, &t128, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t129;
            }
            {
                gm_t88rcucg1b7uk t130 = gm_l22;
                void * t131 = gm_l23;
                gm_tmn7n0n9raou5 t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)3ULL));
                gm_t8jsc4n9vp2ib t133;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t133 + 0), t132);
                GM_ST(void *, ((uint8_t *)&t133 + 8), t131);
                gm_tl874t2atnlh3 t134 = gm_l6;
                gm_to0f5qn75cgg8 t135;
                memset(&t135, 0, sizeof t135);
                GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t135 + 8), t133);
                void * t136 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t130, &t135, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t136;
            }
            {
                gm_t88rcucg1b7uk t137 = gm_l22;
                gm_t88rcucg1b7uk t138 = gm_l12;
                uint32_t t139 = gm_l15;
                gm_tl874t2atnlh3 t140 = gm_l6;
                gm_to0f5qn75cgg8 t141;
                memset(&t141, 0, sizeof t141);
                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t141 + 8), t139);
                void * t142 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t138, &t141, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t143 = gm_l6;
                gm_to0f5qn75cgg8 t144;
                memset(&t144, 0, sizeof t144);
                GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)39ULL));
                GM_ST(void *, ((uint8_t *)&t144 + 8), t142);
                void * t145 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t144, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t145;
            }
            {
                gm_trqfvmjtt4sij t146;
                gm_f_list_58nr42v_2((void *)&t146, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l26 = t146;
            }
            {
                void * t147 = (void *)((uint8_t *)&F.l26);
                gm_t88rcucg1b7uk t148 = gm_l9;
                gm_t88rcucg1b7uk t149 = gm_l13;
                uint32_t t150 = gm_l14;
                gm_tl874t2atnlh3 t151 = gm_l6;
                gm_tb38v5s9j1r32 t152;
                memset(&t152, 0, sizeof t152);
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
                GM_ST(uint32_t, ((uint8_t *)&t152 + 8), t150);
                void * t153 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t149, &t152, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t154 = gm_l6;
                gm_tb38v5s9j1r32 t155;
                memset(&t155, 0, sizeof t155);
                GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)6ULL));
                GM_ST(void *, ((uint8_t *)&t155 + 8), t153);
                void * t156 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t148, &t155, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t157 = gm_l24;
                gm_tbbqefuhv2r8h t158;
                GM_ST(void *, ((uint8_t *)&t158 + 0), t156);
                GM_ST(void *, ((uint8_t *)&t158 + 8), ((void *)0));
                GM_ST(void *, ((uint8_t *)&t158 + 16), t157);
                gm_tftb1odc739t5 t159 = gm_f_push_bpovr2k_2(t147, &t158);
                gm_l36 = t159;
            }
            uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
            if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src4nj79mj9jk57, 397, 5);
            {
                void * t161 = (void *)((uint8_t *)&F.l26);
                gm_t88rcucg1b7uk t162 = gm_l9;
                gm_t88rcucg1b7uk t163 = gm_l12;
                uint32_t t164 = gm_l15;
                gm_tl874t2atnlh3 t165 = gm_l6;
                gm_tb38v5s9j1r32 t166;
                memset(&t166, 0, sizeof t166);
                GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)1ULL));
                GM_ST(uint32_t, ((uint8_t *)&t166 + 8), t164);
                void * t167 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t163, &t166, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t168 = gm_l6;
                gm_tb38v5s9j1r32 t169;
                memset(&t169, 0, sizeof t169);
                GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t169 + 8), t167);
                void * t170 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t162, &t169, t168);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t171 = gm_l25;
                gm_tbbqefuhv2r8h t172;
                GM_ST(void *, ((uint8_t *)&t172 + 0), t170);
                GM_ST(void *, ((uint8_t *)&t172 + 8), ((void *)0));
                GM_ST(void *, ((uint8_t *)&t172 + 16), t171);
                gm_tftb1odc739t5 t173 = gm_f_push_bpovr2k_2(t161, &t172);
                gm_l37 = t173;
            }
            uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
            if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src4nj79mj9jk57, 398, 5);
            {
                gm_t88rcucg1b7uk t175 = gm_l22;
                void * t176 = gm_l11;
                gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                gm_tkaapcii86udk t178;
                GM_ST(void *, ((uint8_t *)&t178 + 0), t176);
                GM_ST(gm_b16_8, ((uint8_t *)&t178 + 8), t177);
                gm_tl874t2atnlh3 t179 = gm_l6;
                gm_to0f5qn75cgg8 t180;
                memset(&t180, 0, sizeof t180);
                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)44ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t180 + 8), t178);
                void * t181 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t175, &t180, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t181;
                goto gm_fret;
            }
        }
gm_lb2:;
        void * t182 = gm_l7;
        gm_t88rcucg1b7uk t183 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t184 = gm_f_empty_slice_g3tddrb_2();
        gm_tl874t2atnlh3 t185 = gm_l6;
        gm_to0f5qn75cgg8 t186;
        memset(&t186, 0, sizeof t186);
        GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t186 + 8), t184);
        void * t187 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t183, &t186, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t187;
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
inline __attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_repeatable_rqn3pcn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    void * t8 = gm_l0;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)15ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)16ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)14ULL));
            if (!t14) {
                goto gm_lb4;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)17ULL));
            if (!t16) {
                goto gm_lb5;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 250, 53);
    }
gm_lb1:;
    uint8_t t17 = gm_l1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_scatter_h5ruqu8 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    void * gm_l4; /* x */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l6; /* fields */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l12; /* col */
    void * gm_l13; /* place */
    void * gm_l14; /* part */
    GM_STACK_CHECK(&F, 826u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t8ta482bl6be0 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_f_soa_fields_dg06fgc(t7, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l6 = t8;
    }
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_ir0h3tc_2((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t9;
    }
    gm_b16_8 t10 = gm_l6;
    gm_l8 = t10;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l9;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t14 = gm_l9;
                    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t17 = gm_l9;
                    gm_t0mglksrkfa5e t18 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t15) + (uintptr_t)t17 * 40));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l15 + 0), t14);
                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l15 + 8), t18);
                }
                {
                    {
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                        gm_l10 = t19;
                        {
                            memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l15) + 8), 40);
                        }
                        {
                            void * t20 = gm_l0;
                            void * t21 = gm_l1;
                            uint64_t t22 = gm_l10;
                            gm_tl874t2atnlh3 t23 = gm_l5;
                            void * t24 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t20, t21, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t22, t23);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l12 = t24;
                        }
                        {
                            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                            void * t26 = gm_l12;
                            void * t27 = gm_l3;
                            void * t28 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t27);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tlcl4u558l6p5 t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
                            GM_ST(void *, ((uint8_t *)&t29 + 0), t26);
                            GM_ST(void *, ((uint8_t *)&t29 + 8), t28);
                            GM_ST(uint8_t, ((uint8_t *)&t29 + 16), ((uint8_t)1));
                            gm_tl874t2atnlh3 t30 = gm_l5;
                            gm_to0f5qn75cgg8 t31;
                            memset(&t31, 0, sizeof t31);
                            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t31 + 8), t29);
                            void * t32 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t25, &t31, t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l13 = t32;
                        }
                        {
                            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                            void * t34 = gm_l4;
                            void * t35 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t36 = gm_l10;
                            uint32_t t37 = (uint32_t)t36;
                            gm_tbl8il92l97ut t38;
                            GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                            GM_ST(void *, ((uint8_t *)&t38 + 0), t35);
                            GM_ST(uint32_t, ((uint8_t *)&t38 + 8), t37);
                            gm_tl874t2atnlh3 t39 = gm_l5;
                            gm_to0f5qn75cgg8 t40;
                            memset(&t40, 0, sizeof t40);
                            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)19ULL));
                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t40 + 8), t38);
                            void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t33, &t40, t39);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l14 = t41;
                        }
                        {
                            void * t42 = (void *)((uint8_t *)&F.l7);
                            void * t43 = gm_l13;
                            void * t44 = gm_l14;
                            gm_t3mf58gv9kf3n t45;
                            GM_ST(void *, ((uint8_t *)&t45 + 0), t43);
                            GM_ST(void *, ((uint8_t *)&t45 + 8), t44);
                            gm_trcmmd12cefb1 t46;
                            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t46 + 8), t45);
                            gm_tftb1odc739t5 t47 = gm_f_push_d6c76ud_2(t42, &t46);
                            gm_l16 = t47;
                        }
                        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src4nj79mj9jk57, 293, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 289, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t49 = gm_l9;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l9 = t52;
        }
    }
gm_lb1:;
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    F.ret = t53;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1) {
    struct gm_fr_soa_shape_755ppqp F;
    gm_tf5srh4mp4b3i gm_l0; /* len */
    uint8_t gm_l1; /* is_const */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 gm_l4;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l0))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
            gm_l2 = t5;
            uint64_t t6 = gm_l2;
            gm_t4ce0jd3tdro4 t7;
            GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t6);
            gm_f_format_d04m6io((void *)&gm_l3, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src4nj79mj9jk57, 229, 26);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t11 = gm_l1;
    if (t11) {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_shsv7bl83q04d));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(8));
        gm_l4 = t12;
    } else {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s3ndsddjcvskd));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(2));
        gm_l4 = t13;
    }
    gm_b16_8 t14 = gm_l4;
    F.ret = t14;
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
inline __attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_shape_of_ohenvjc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l4; /* tm */
    GM_STACK_CHECK_SHALLOW(&F, 199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 2536));
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
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 32), 32);
                        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t16 = gm_l1;
                        uint8_t t17 = ((t15 == t16));
                        if (t17) {
                            gm_tf5srh4mp4b3i t18 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l4) + 8));
                            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 24));
                            gm_th3eueiipivq6 t20;
                            GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t20 + 0), t18);
                            GM_ST(uint8_t, ((uint8_t *)&t20 + 16), t19);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 28, 0);
                            GM_ST(gm_th3eueiipivq6, ((uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out)), t20);
                            ((uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out))[24] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 170, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l3;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l3 = t24;
        }
    }
gm_lb1:;
    memset(&(*(gm_tbm0n9ujhrlhf *)gm_out), 0, sizeof (*(gm_tbm0n9ujhrlhf *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1) { gm_tbm0n9ujhrlhf gm_r; gm_f_soa_shape_of_ohenvjc(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9) {
    struct gm_fr_soa_slice_column_6ogdk5f F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    uint8_t gm_l4; /* is_const */
    void * gm_l5; /* lo */
    void * gm_l6; /* hi */
    uint8_t gm_l7; /* inclusive */
    gm_t88rcucg1b7uk gm_l8; /* ft */
    gm_tl874t2atnlh3 gm_l9; /* span */
    void * gm_l10; /* col */
    void * gm_l11; /* l */
    void * gm_l12; /* x */
    void * gm_l13; /* h */
    void * gm_l14; /* x */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    gm_l9 = a9;
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        uint64_t t8 = gm_l3;
        gm_tl874t2atnlh3 t9 = gm_l9;
        void * t10 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t6, t7, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t10;
    }
    gm_l11 = ((void *)0);
    {
        {
            uint8_t t11 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = GM_LD(void *, ((uint8_t *)&gm_l5));
            gm_l12 = t12;
            void * t13 = gm_l12;
            void * t14 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t14;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_l13 = ((void *)0);
    {
        {
            uint8_t t15 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
            if (!t15) {
                goto gm_lb4;
            }
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l6));
            gm_l14 = t16;
            void * t17 = gm_l14;
            void * t18 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t18;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        uint8_t t21 = gm_l4;
        gm_t88rcucg1b7uk t22 = gm_l8;
        gm_t88rcucg1b7uk t23 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t24 = gm_l10;
        void * t25 = gm_l11;
        void * t26 = gm_l13;
        uint8_t t27 = gm_l7;
        gm_tao63pk3r18v5 t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 28, 0);
        GM_ST(void *, ((uint8_t *)&t28 + 0), t24);
        GM_ST(void *, ((uint8_t *)&t28 + 8), t25);
        GM_ST(void *, ((uint8_t *)&t28 + 16), t26);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 24), t27);
        gm_tl874t2atnlh3 t29 = gm_l9;
        gm_to0f5qn75cgg8 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t30 + 8), t28);
        void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t23, &t30, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t31;
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
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_soa_slice_columns_kqq8dfr F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint8_t gm_l3; /* is_const */
    void * gm_l4; /* lo */
    void * gm_l5; /* hi */
    uint8_t gm_l6; /* inclusive */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* fields */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    void * gm_l15; /* sl */
    void * gm_l16; /* first */
    GM_STACK_CHECK(&F, 867u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_l8 = t8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)a2));
    gm_tf5srh4mp4b3i t11;
    memset(&t11, 0, sizeof t11);
    uint8_t t12 = gm_l3;
    gm_tl874t2atnlh3 t13 = gm_l7;
    gm_t88rcucg1b7uk t14 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t9, t10, t11, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t14;
    {
        void * t15 = gm_l0;
        gm_b16_8 t16 = gm_f_soa_fields_dg06fgc(t15, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l10 = t16;
    }
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_vp1djv1_2((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t17;
    }
    gm_l13 = ((uint64_t)0ULL);
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
    gm_l12 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l13;
            uint64_t t20 = gm_l12;
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t22 = gm_l13;
                        gm_l14 = t22;
                        {
                            void * t23 = gm_l0;
                            void * t24 = gm_l1;
                            uint64_t t25 = gm_l14;
                            uint8_t t26 = gm_l3;
                            void * t27 = gm_l4;
                            void * t28 = gm_l5;
                            uint8_t t29 = gm_l6;
                            void * t30 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t32 = gm_l14;
                            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t30) + (uintptr_t)t32 * 40) + 16));
                            gm_tl874t2atnlh3 t34 = gm_l7;
                            void * t35 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t23, t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t25, t26, t27, t28, t29, t33, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t35;
                        }
                        {
                            void * t36 = (void *)((uint8_t *)&F.l11);
                            void * t37 = gm_l8;
                            uint8_t t38 = gm_l3;
                            void * t39 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t41 = gm_l14;
                            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t39) + (uintptr_t)t41 * 40) + 16));
                            gm_t88rcucg1b7uk t43 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t37, t38, t42);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t44 = gm_l15;
                            gm_tl874t2atnlh3 t45 = gm_l7;
                            gm_to0f5qn75cgg8 t46;
                            memset(&t46, 0, sizeof t46);
                            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)23ULL));
                            GM_ST(void *, ((uint8_t *)&t46 + 8), t44);
                            void * t47 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t46, t45);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t48 = gm_f_push_d943vgp_2(t36, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t47)));
                            gm_l17 = t48;
                        }
                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src4nj79mj9jk57, 306, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 304, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t50 = gm_l13;
            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
            uint64_t t53 = r51;
            gm_l13 = t53;
        }
    }
gm_lb1:;
    {
        void * t54 = gm_l0;
        void * t55 = gm_l1;
        uint8_t t56 = gm_l3;
        void * t57 = gm_l4;
        void * t58 = gm_l5;
        uint8_t t59 = gm_l6;
        void * t60 = GM_LD(void *, ((uint8_t *)&gm_l10));
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_src4nj79mj9jk57, 308, 74);
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 40) + 16));
        gm_tl874t2atnlh3 t63 = gm_l7;
        void * t64 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t54, t55, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), ((uint64_t)0ULL), t56, t57, t58, t59, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t64;
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l11);
        void * t66 = gm_l8;
        gm_t88rcucg1b7uk t67 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t68 = gm_l16;
        gm_tl874t2atnlh3 t69 = gm_l7;
        gm_to0f5qn75cgg8 t70;
        memset(&t70, 0, sizeof t70);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t70 + 8), t68);
        void * t71 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t67, &t70, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t72 = gm_f_push_d943vgp_2(t65, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t71)));
        gm_l18 = t72;
    }
    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src4nj79mj9jk57, 309, 5);
    {
        gm_t88rcucg1b7uk t74 = gm_l9;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_tl874t2atnlh3 t76 = gm_l7;
        gm_to0f5qn75cgg8 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t75);
        void * t78 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t74, &t77, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t78;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_stand_in_kdtmkn7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    uint8_t t4 = gm_l3;
    uint32_t t5 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        uint32_t t8 = gm_l4;
        gm_t88rcucg1b7uk t9 = gm_l1;
        gm_toa2vhe3l4hfr t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t10 + 8), t9);
        gm_t2ubvi1j1aq22 t11;
        GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t11 + 0), t10);
        F.l5 = t11;
        void * t12 = (void *)((uint8_t *)&F.l5);
        uint64_t t13 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        gm_t88rcucg1b7uk t15 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t7, t8, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t15;
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
inline __attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_temp_2pshn8g F;
    void * gm_l0; /* locals */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = gm_l2;
    gm_tsoie1ppgdvcg t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 21, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 22, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 16), t4);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 20), ((uint8_t)1));
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 24), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_am7bo0v(t2, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src4nj79mj9jk57, 245, 5);
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 246, 13);
    uint64_t t13 = r11;
    uint32_t t14 = (uint32_t)t13;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2) {
    struct gm_fr_soa_template_fa784hs F;
    void * gm_l0; /* s */
    gm_tf5srh4mp4b3i gm_l1; /* len */
    uint8_t gm_l2; /* is_const */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l5; /* tm */
    gm_b16_8 gm_l6; /* name */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 2536));
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
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t14) + (uintptr_t)t16 * 32), 32);
                        {
                            {
                                gm_tf5srh4mp4b3i t17 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l5) + 8));
                                gm_tf5srh4mp4b3i t18 = gm_l1;
                                uint8_t t19 = gm_f_same_len_bnkvi6r(t17, t18);
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
                                uint8_t t21 = gm_l2;
                                uint8_t t22 = ((t20 == t21));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                                F.ret = t23;
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 158, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l4;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l4 = t27;
        }
    }
gm_lb1:;
    {
        gm_tf5srh4mp4b3i t28 = gm_l1;
        uint8_t t29 = gm_l2;
        gm_b16_8 t30 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_f_format_thmq1t9((void *)&gm_l8, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src4nj79mj9jk57, 161, 13);
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_l6 = t33;
    void * t34 = gm_l0;
    void * t35 = (void *)(((uint8_t *)t34) + 8);
    gm_b16_8 t36 = gm_l6;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    uint32_t t38 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t35, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t38;
    {
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 1088);
        gm_b16_8 t41 = gm_l6;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_ti0kgh3pc3in2 t43;
        gm_f_empty_struct_b5i94nj((void *)&t43);
        gm_t6art7bprjiqb t44;
        gm_f_compiler_adt_eiq6rf1((void *)&t44, (void *)NULL, cx, NULL, t42, ((uint64_t)0ULL), &t43, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t45 = gm_f_push_j84ih6c(t40, &t44);
        gm_l9 = t45;
    }
    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src4nj79mj9jk57, 163, 5);
    {
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 2536);
        uint32_t t49 = gm_l7;
        gm_tf5srh4mp4b3i t50 = gm_l1;
        uint8_t t51 = gm_l2;
        gm_tj4mbomtvccf0 t52;
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t52 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 28, 0);
        GM_ST(uint32_t, ((uint8_t *)&t52 + 0), t49);
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t52 + 8), t50);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 24), t51);
        gm_tftb1odc739t5 t53 = gm_f_push_3dc3qci(t48, &t52);
        gm_l10 = t53;
    }
    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src4nj79mj9jk57, 164, 5);
    uint32_t t55 = gm_l7;
    F.ret = t55;
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
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_to_const_hhiu49k F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* src */
    gm_t88rcucg1b7uk gm_l3; /* target */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* target_fields */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l11; /* part */
    GM_STACK_CHECK(&F, 854u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_th2lqbo77pvsc gm_l12;
    gm_th2lqbo77pvsc gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l2;
        gm_f_struct_fields_chqu3it((void *)&gm_l12, t7, GM_CONV(gm_csguankko4lmhd, t8));
    }
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
        gm_l5 = t10;
    } else {
        gm_b16_8 t11 = gm_f_empty_slice_ose1ba9();
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l3;
        gm_f_struct_fields_chqu3it((void *)&gm_l13, t12, GM_CONV(gm_csguankko4lmhd, t13));
    }
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
    if (t14) {
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
        gm_l6 = t15;
    } else {
        gm_b16_8 t16 = gm_f_empty_slice_ose1ba9();
        gm_l6 = t16;
    }
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_vp1djv1_2((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t17;
    }
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    uint64_t t20 = (t18 < t19 ? t18 : t19);
    gm_l8 = t20;
    {
        for (;;) {
            uint64_t t21 = gm_l9;
            uint64_t t22 = gm_l8;
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t24 = gm_l9;
                        gm_l10 = t24;
                        void * t25 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t27 = gm_l10;
                        if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_src4nj79mj9jk57, 328, 24);
                        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t25) + (uintptr_t)t27 * 40) + 16));
                        void * t29 = gm_l1;
                        void * t30 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t31 = gm_l10;
                        uint32_t t32 = (uint32_t)t31;
                        gm_tbl8il92l97ut t33;
                        GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t33 + 0), t30);
                        GM_ST(uint32_t, ((uint8_t *)&t33 + 8), t32);
                        gm_tl874t2atnlh3 t34 = gm_l4;
                        gm_to0f5qn75cgg8 t35;
                        memset(&t35, 0, sizeof t35);
                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t35 + 8), t33);
                        void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t35, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t36;
                        void * t37 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t39 = gm_l10;
                        if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_src4nj79mj9jk57, 329, 12);
                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t37) + (uintptr_t)t39 * 40) + 16));
                        void * t41 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t43 = gm_l10;
                        if ((uintptr_t)t43 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t43, t42, gm_src4nj79mj9jk57, 329, 28);
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t41) + (uintptr_t)t43 * 40) + 16));
                        uint8_t t45 = (gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t44)));
                        if (t45) {
                            void * t46 = (void *)((uint8_t *)&F.l7);
                            void * t47 = gm_l11;
                            gm_tftb1odc739t5 t48 = gm_f_push_d943vgp_2(t46, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t47)));
                            gm_l14 = t48;
                            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src4nj79mj9jk57, 330, 13);
                        } else {
                            {
                                void * t50 = (void *)((uint8_t *)&F.l7);
                                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t53 = gm_l10;
                                if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_src4nj79mj9jk57, 332, 31);
                                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t51) + (uintptr_t)t53 * 40) + 16));
                                void * t55 = gm_l11;
                                gm_tmn7n0n9raou5 t56;
                                GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)6ULL));
                                gm_t8jsc4n9vp2ib t57;
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t57 + 0), t56);
                                GM_ST(void *, ((uint8_t *)&t57 + 8), t55);
                                gm_tl874t2atnlh3 t58 = gm_l4;
                                gm_to0f5qn75cgg8 t59;
                                memset(&t59, 0, sizeof t59);
                                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)32ULL));
                                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t59 + 8), t57);
                                void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t59, t58);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t61 = gm_f_push_d943vgp_2(t50, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t60)));
                                gm_l15 = t61;
                            }
                            uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src4nj79mj9jk57, 332, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 327, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t63 = gm_l9;
            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
            uint64_t t66 = r64;
            gm_l9 = t66;
        }
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t67 = gm_l3;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_tl874t2atnlh3 t69 = gm_l4;
        gm_to0f5qn75cgg8 t70;
        memset(&t70, 0, sizeof t70);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t68);
        void * t71 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t67, &t70, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t71;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_type_42lvsj0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_te3magq8drarp gm_l7;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_l2;
    uint8_t t8 = gm_l3;
    gm_f_soa_type_of_q8vtd5j((void *)&gm_l7, (void *)NULL, cx, NULL, t5, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
            gm_l5 = t11;
            gm_t88rcucg1b7uk t12 = gm_l5;
            F.ret = t12;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_l6 = t15;
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = gm_l4;
            gm_b16_8 t18 = gm_l6;
            gm_b0_1 t19 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            gm_t88rcucg1b7uk t22 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t22;
            goto gm_fret;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 84, 5);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_type_expr_go3p0n7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t3mf58gv9kf3n gm_l4; /* a */
    void * gm_l5; /* len */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* is_const */
    void * gm_l10; /* inner_ty */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    GM_STACK_CHECK(&F, 518u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t5e6kkuq21nig gm_l12;
    void * t12 = gm_l2;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)28ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_t3mf58gv9kf3n t15 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t12) + 8) + 8));
            gm_l4 = t15;
            {
                {
                    uint8_t t16 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
                    if (!t16) {
                        goto gm_lb4;
                    }
                    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    gm_l5 = t17;
                    void * t18 = gm_l0;
                    uint64_t t19 = gm_l1;
                    void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                    gm_t88rcucg1b7uk t21 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t18, t19, t20);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t21;
                    gm_l7 = ((uint64_t)0ULL);
                    void * t22 = gm_l0;
                    uint64_t t23 = gm_l1;
                    void * t24 = gm_l5;
                    gm_t5e6kkuq21nig t25 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t22, t23, t24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t25;
                    {
                        {
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                            if (!t27) {
                                goto gm_lb6;
                            }
                            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            gm_l8 = t28;
                            uint64_t t29 = gm_l8;
                            gm_l7 = t29;
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
                            if (!t31) {
                                goto gm_lb7;
                            }
                            void * t32 = gm_l0;
                            void * t33 = gm_l5;
                            gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                            gm_b16_8 t35;
                            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s58rgrgmho9sa));
                            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(67));
                            gm_b0_1 t36 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t34, t35);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t37 = gm_l0;
                            void * t38 = (void *)(((uint8_t *)t37) + 8);
                            gm_t88rcucg1b7uk t39 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t38);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t39;
                            goto gm_fret;
                        }
gm_lb7:;
                        {
                            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
                            if (!t41) {
                                goto gm_lb8;
                            }
                            void * t42 = gm_l0;
                            void * t43 = (void *)(((uint8_t *)t42) + 8);
                            gm_t88rcucg1b7uk t44 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t43);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t44;
                            goto gm_fret;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 207, 13);
                    }
gm_lb5:;
                    void * t45 = gm_l0;
                    gm_t88rcucg1b7uk t46 = gm_l6;
                    uint64_t t47 = gm_l7;
                    gm_tf5srh4mp4b3i t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t48), t47);
                    ((uint8_t *)&t48)[8] = 1;
                    gm_tl874t2atnlh3 t49 = gm_l3;
                    gm_t88rcucg1b7uk t50 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t45, t46, t48, ((uint8_t)0), t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t50;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)26ULL));
            if (!t52) {
                goto gm_lb9;
            }
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 8));
            gm_l9 = t53;
            void * t54 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
            gm_l10 = t54;
            void * t55 = gm_l0;
            uint64_t t56 = gm_l1;
            void * t57 = gm_l10;
            gm_t88rcucg1b7uk t58 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t55, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t58;
            void * t59 = gm_l0;
            gm_t88rcucg1b7uk t60 = gm_l11;
            gm_tf5srh4mp4b3i t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = gm_l9;
            gm_tl874t2atnlh3 t63 = gm_l3;
            gm_t88rcucg1b7uk t64 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t59, t60, t61, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t64;
            goto gm_fret;
        }
gm_lb9:;
        {
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 203, 5);
    }
gm_lb1:;
    void * t65 = gm_l0;
    gm_tl874t2atnlh3 t66 = gm_l3;
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_ssnb4or831s5l));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(77));
    gm_b0_1 t68 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t69 = gm_l0;
    void * t70 = (void *)(((uint8_t *)t69) + 8);
    gm_t88rcucg1b7uk t71 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t70);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_type_of_q8vtd5j F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const0 */
    void * gm_l4; /* tys */
    uint8_t gm_l5; /* is_const */
    gm_b16_8 gm_l6; /* not_struct */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l9; /* fields */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tcu60n24kv26r gm_l16; /* m */
    gm_b16_8 gm_l17; /* name */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l22; /* n */
    gm_t88rcucg1b7uk gm_l23; /* ct */
    uint32_t gm_l24; /* template */
    uint32_t gm_l25; /* soa */
    gm_t88rcucg1b7uk gm_l27; /* t */
    GM_STACK_CHECK(&F, 2519u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    gm_t53at80pfdus3 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_t53at80pfdus3 gm_l33;
    uint8_t gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tftb1odc739t5 gm_l39;
    uint8_t gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    {
        void * t41 = gm_l0;
        void * t42 = (void *)(((uint8_t *)t41) + 8);
        gm_l4 = t42;
        void * t43 = gm_l4;
        gm_t88rcucg1b7uk t44 = gm_l1;
        uint8_t t45 = gm_f_is_error_8k27gd1(t43, GM_CONV(gm_csguankko4lmhd, t44));
        if (t45) {
            gm_t88rcucg1b7uk t46 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t46);
            goto gm_fdone;
        }
        uint8_t t47 = gm_l3;
        if (t47) {
            {
                {
                    uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                    if (t48) {
                        goto gm_lb2;
                    }
                    gm_l29 = ((uint8_t)1);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_l29 = ((uint8_t)0);
                    goto gm_lb1;
                }
gm_lb3:;
                gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 98, 31);
            }
gm_lb1:;
            uint8_t t49 = gm_l29;
            gm_l28 = t49;
        } else {
            gm_l28 = ((uint8_t)0);
        }
        uint8_t t50 = gm_l28;
        gm_l5 = t50;
        {
            {
                void * t51 = gm_l4;
                gm_t88rcucg1b7uk t52 = gm_l1;
                uint8_t t53 = gm_f_has_param_u9t0efs(t51, GM_CONV(gm_csguankko4lmhd, t52));
                if (!t53) {
                    goto gm_lb5;
                }
                {
                    void * t54 = gm_l4;
                    gm_t88rcucg1b7uk t55 = gm_l1;
                    gm_f_kind_jcnt4hm((void *)&gm_l31, t54, GM_CONV(gm_csguankko4lmhd, t55));
                }
                {
                    {
                        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t57 = (uint8_t)(t56 == ((uint8_t)19ULL));
                        if (!t57) {
                            goto gm_lb7;
                        }
                        gm_l30 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                        uint8_t t59 = (uint8_t)(t58 == ((uint8_t)17ULL));
                        if (!t59) {
                            goto gm_lb8;
                        }
                        gm_l30 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb8:;
                    {
                        gm_l30 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 99, 32);
                }
gm_lb6:;
                uint8_t t60 = gm_l30;
                if (!t60) {
                    goto gm_lb5;
                }
                {
                    void * t61 = gm_l0;
                    gm_t88rcucg1b7uk t62 = gm_l1;
                    gm_tf5srh4mp4b3i t63 = gm_l2;
                    uint8_t t64 = gm_l5;
                    gm_t88rcucg1b7uk t65 = gm_f_soa_stand_in_kdtmkn7((void *)NULL, cx, NULL, t61, t62, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t65);
                    goto gm_fdone;
                }
            }
gm_lb5:;
        }
gm_lb4:;
    }
    {
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l1;
        gm_b16_8 t68 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t69;
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
        gm_f_format_16dceus((void *)&gm_l32, (void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src4nj79mj9jk57, 100, 19);
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
        gm_l6 = t71;
        {
            {
                {
                    {
                        void * t72 = gm_l4;
                        gm_t88rcucg1b7uk t73 = gm_l1;
                        gm_f_kind_jcnt4hm((void *)&gm_l33, t72, GM_CONV(gm_csguankko4lmhd, t73));
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                        uint8_t t75 = (uint8_t)(t74 == ((uint8_t)17ULL));
                        if (!t75) {
                            goto gm_lb11;
                        }
                        uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 8));
                        gm_l7 = t76;
                        void * t77 = gm_l0;
                        void * t78 = GM_LD(void *, (((uint8_t *)t77) + 1088));
                        uint64_t t79 = GM_LD(uint64_t, ((((uint8_t *)t77) + 1088) + 8));
                        uint32_t t80 = gm_l7;
                        uint64_t t81 = (uint64_t)t80;
                        if ((uintptr_t)t81 >= (uintptr_t)(t79)) gm_trap_index((uintptr_t)t81, t79, gm_src4nj79mj9jk57, 102, 13);
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t78) + (uintptr_t)t81 * 184), 184);
                        gm_b16_8 t82 = gm_f_empty_slice_ose1ba9();
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                        gm_l9 = t83;
                        {
                            {
                                uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
                                uint8_t t85 = (uint8_t)(t84 == ((uint8_t)0ULL));
                                if (!t85) {
                                    goto gm_lb13;
                                }
                                memmove(((uint8_t *)&F.l10), ((((uint8_t *)&F.l8) + 24) + 8), 48);
                                {
                                    {
                                        uint8_t t86 = (uint8_t)(((((uint8_t *)&F.l10) + 40))[4] != 0);
                                        if (t86) {
                                            goto gm_lb15;
                                        }
                                        gm_l34 = ((uint8_t)0);
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    {
                                        gm_l34 = ((uint8_t)1);
                                        goto gm_lb14;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 106, 16);
                                }
gm_lb14:;
                                uint8_t t87 = gm_l34;
                                if (t87) {
                                    {
                                        void * t88 = gm_l0;
                                        gm_t88rcucg1b7uk t89 = gm_l1;
                                        gm_b16_8 t90 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t88, t89);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t91;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
                                        gm_f_format_fambdq4((void *)&gm_l35, (void *)NULL, cx, NULL, t91);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                    if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src4nj79mj9jk57, 106, 51);
                                    {
                                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)1ULL));
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t94);
                                        goto gm_fdone;
                                    }
                                }
                                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                                gm_l9 = t96;
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                gm_b16_8 t97 = gm_l6;
                                gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)1ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t98);
                                goto gm_fdone;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 104, 5);
                        }
gm_lb12:;
                        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint8_t t100 = ((t99 == ((uint64_t)0ULL)));
                        if (t100) {
                            {
                                void * t101 = gm_l0;
                                gm_t88rcucg1b7uk t102 = gm_l1;
                                gm_b16_8 t103 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t101, t102);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t104;
                                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                                gm_f_format_4nn4ect((void *)&gm_l36, (void *)NULL, cx, NULL, t104);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src4nj79mj9jk57, 111, 38);
                            {
                                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)1ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t107);
                                goto gm_fdone;
                            }
                        }
                    }
                    {
                        gm_b16_8 t108 = gm_l9;
                        gm_l11 = t108;
                        gm_l12 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t109 = gm_l12;
                                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t111 = (t109 < t110);
                                if (!t111) {
                                    goto gm_lb18;
                                }
                                {
                                    void * t112 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint64_t t114 = gm_l12;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t112) + (uintptr_t)t114 * 40), 40);
                                            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                            gm_b16_8 t116;
                                            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sm2k5tp7dtg07));
                                            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(3));
                                            uint8_t t117 = (gm_eq0iqiubfm7e(((uint8_t *)&t115), ((uint8_t *)&t116)));
                                            if (t117) {
                                                {
                                                    void * t118 = gm_l0;
                                                    gm_t88rcucg1b7uk t119 = gm_l1;
                                                    gm_b16_8 t120 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t118, t119);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tnpifv20irnph t121;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t120);
                                                    gm_f_format_3dfoht6((void *)&gm_l37, (void *)NULL, cx, NULL, t121);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src4nj79mj9jk57, 113, 42);
                                                {
                                                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)1ULL));
                                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t124);
                                                    goto gm_fdone;
                                                }
                                            }
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 112, 5);
                                    }
gm_lb21:;
                                }
gm_lb20:;
                                uint64_t t125 = gm_l12;
                                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                                uint64_t t128 = r126;
                                gm_l12 = t128;
                            }
                        }
gm_lb18:;
                        void * t129 = gm_l0;
                        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)t129) + 2496));
                        gm_l14 = t130;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t131 = gm_l15;
                                uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint8_t t133 = (t131 < t132);
                                if (!t133) {
                                    goto gm_lb23;
                                }
                                {
                                    void * t134 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint64_t t136 = gm_l15;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l16), (((uint8_t *)t134) + (uintptr_t)t136 * 32), 32);
                                            {
                                                {
                                                    gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l16));
                                                    gm_t88rcucg1b7uk t138 = gm_l1;
                                                    uint8_t t139 = (gm_eq9m6mvtmgde(((uint8_t *)&t137), ((uint8_t *)&t138)));
                                                    if (!t139) {
                                                        goto gm_lb29;
                                                    }
                                                    gm_tf5srh4mp4b3i t140 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l16) + 8));
                                                    gm_tf5srh4mp4b3i t141 = gm_l2;
                                                    uint8_t t142 = gm_f_same_len_bnkvi6r(t140, t141);
                                                    if (!t142) {
                                                        goto gm_lb29;
                                                    }
                                                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 24));
                                                    uint8_t t144 = gm_l5;
                                                    uint8_t t145 = ((t143 == t144));
                                                    if (!t145) {
                                                        goto gm_lb29;
                                                    }
                                                    gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 28));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t146);
                                                    goto gm_fdone;
                                                }
gm_lb29:;
                                            }
gm_lb28:;
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 115, 5);
                                    }
gm_lb26:;
                                }
gm_lb25:;
                                uint64_t t147 = gm_l15;
                                uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                uint64_t t150 = r148;
                                gm_l15 = t150;
                            }
                        }
gm_lb23:;
                    }
                    {
                        gm_tf5srh4mp4b3i t151 = gm_l2;
                        uint8_t t152 = gm_l5;
                        gm_b16_8 t153 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t151, t152);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t154 = gm_l0;
                        gm_t88rcucg1b7uk t155 = gm_l1;
                        gm_b16_8 t156 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t154, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t157;
                        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 0), t153);
                        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 16), t156);
                        gm_f_format_j0i128e((void *)&gm_l38, (void *)NULL, cx, NULL, &t157);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                        if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src4nj79mj9jk57, 118, 13);
                        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                        gm_l17 = t159;
                        gm_trqfvmjtt4sij t160;
                        gm_f_list_spqn36o((void *)&t160, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l18 = t160;
                        gm_b16_8 t161 = gm_l9;
                        gm_l19 = t161;
                        gm_l20 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t162 = gm_l20;
                                uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                uint8_t t164 = (t162 < t163);
                                if (!t164) {
                                    goto gm_lb30;
                                }
                                {
                                    void * t165 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                    uint64_t t167 = gm_l20;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l21), (((uint8_t *)t165) + (uintptr_t)t167 * 40), 40);
                                            }
                                            {
                                                {
                                                    uint8_t t168 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                                                    if (!t168) {
                                                        goto gm_lb36;
                                                    }
                                                    uint64_t t169 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                                                    gm_l22 = t169;
                                                    void * t170 = gm_l4;
                                                    uint64_t t171 = gm_l22;
                                                    gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                                                    gm_t88rcucg1b7uk t173 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t170, t171, t172);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l23 = t173;
                                                    goto gm_lb35;
                                                }
gm_lb36:;
                                                void * t174 = gm_l4;
                                                uint8_t t175 = gm_l5;
                                                gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                                                gm_t88rcucg1b7uk t177 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t174, t175, t176);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l23 = t177;
                                            }
gm_lb35:;
                                            {
                                                void * t178 = (void *)((uint8_t *)&F.l18);
                                                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                                gm_t88rcucg1b7uk t180 = gm_l23;
                                                gm_tf5srh4mp4b3i t181;
                                                memset(&t181, 0, sizeof t181);
                                                gm_t9qkh0rsooc8d t182;
                                                GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)2ULL));
                                                gm_t0mglksrkfa5e t183;
                                                GM_ST(uint8_t, (uint8_t *)&t183 + 21, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t183 + 22, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t183 + 0), t179);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t183 + 16), t180);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t183 + 20), t182);
                                                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t183 + 24), t181);
                                                gm_tftb1odc739t5 t184 = gm_f_push_vrmo7c5(t178, &t183);
                                                gm_l39 = t184;
                                            }
                                            uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                            if (!t185) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src4nj79mj9jk57, 122, 9);
                                            goto gm_lb33;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 120, 5);
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                uint64_t t186 = gm_l20;
                                uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                                uint64_t t189 = r187;
                                gm_l20 = t189;
                            }
                        }
gm_lb30:;
                    }
                    {
                        {
                            {
                                uint8_t t190 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                                if (t190) {
                                    goto gm_lb38;
                                }
                                gm_l40 = ((uint8_t)1);
                                goto gm_lb37;
                            }
gm_lb38:;
                            {
                                gm_l40 = ((uint8_t)0);
                                goto gm_lb37;
                            }
gm_lb39:;
                            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 124, 8);
                        }
gm_lb37:;
                        uint8_t t191 = gm_l40;
                        if (t191) {
                            void * t192 = (void *)((uint8_t *)&F.l18);
                            gm_b16_8 t193;
                            GM_ST(void *, (uint8_t *)&t193, (void *)(gm_sm2k5tp7dtg07));
                            GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(3));
                            void * t194 = gm_l4;
                            gm_t88rcucg1b7uk t195 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t194);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf5srh4mp4b3i t196;
                            memset(&t196, 0, sizeof t196);
                            gm_t9qkh0rsooc8d t197;
                            GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)2ULL));
                            gm_t0mglksrkfa5e t198;
                            GM_ST(uint8_t, (uint8_t *)&t198 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t198 + 22, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t193);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t198 + 16), t195);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t198 + 20), t197);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t198 + 24), t196);
                            gm_tftb1odc739t5 t199 = gm_f_push_vrmo7c5(t192, &t198);
                            gm_l41 = t199;
                            uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src4nj79mj9jk57, 124, 22);
                        }
                        void * t201 = gm_l0;
                        gm_tf5srh4mp4b3i t202 = gm_l2;
                        uint8_t t203 = gm_l5;
                        uint32_t t204 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t201, t202, t203);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l24 = t204;
                        void * t205 = gm_l4;
                        gm_b16_8 t206 = gm_l17;
                        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                        uint32_t t208 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t205, t207);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l25 = t208;
                        void * t209 = gm_l4;
                        uint32_t t210 = gm_l25;
                        uint32_t t211 = gm_l24;
                        gm_f_set_template_o09q96o(t209, t210, t211);
                        void * t212 = gm_l0;
                        void * t213 = (void *)(((uint8_t *)t212) + 1088);
                        gm_b16_8 t214 = gm_l17;
                        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                        uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
                        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                        gm_tf5srh4mp4b3i t218;
                        memset(&t218, 0, sizeof t218);
                        gm_tkn42u7na3t81 t219;
                        memset(&t219, 0, sizeof t219);
                        gm_tpautr4br7h3l t220;
                        GM_ST(uint8_t, (uint8_t *)&t220 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t220 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t220 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t217);
                        GM_ST(uint8_t, ((uint8_t *)&t220 + 16), ((uint8_t)0));
                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t220 + 24), t218);
                        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t220 + 40), t219);
                        uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
                        gm_ti0kgh3pc3in2 t222;
                        GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t222 + 8), t220);
                        gm_t6art7bprjiqb t223;
                        gm_f_compiler_adt_eiq6rf1((void *)&t223, (void *)NULL, cx, NULL, t215, t216, &t222, t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t224 = gm_f_push_j84ih6c(t213, &t223);
                        gm_l42 = t224;
                        uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src4nj79mj9jk57, 129, 5);
                        void * t226 = gm_l0;
                        void * t227 = (void *)(((uint8_t *)t226) + 2440);
                        uint32_t t228 = gm_l25;
                        gm_t88rcucg1b7uk t229 = gm_l1;
                        gm_tf5srh4mp4b3i t230 = gm_l2;
                        uint8_t t231 = gm_l5;
                        gm_tjpqvfd72o62u t232;
                        GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t232 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t232 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t232 + 28, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t232 + 0), t229);
                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t232 + 8), t230);
                        GM_ST(uint8_t, ((uint8_t *)&t232 + 24), t231);
                        gm_tftb1odc739t5 t233 = gm_f_put_ftmshk7(t227, t228, &t232);
                        gm_l43 = t233;
                        uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                        if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src4nj79mj9jk57, 130, 5);
                    }
                    {
                        void * t235 = gm_l4;
                        uint32_t t236 = gm_l25;
                        gm_t88rcucg1b7uk t237 = gm_l1;
                        gm_toa2vhe3l4hfr t238;
                        memset(&t238, 0, sizeof t238);
                        GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t238 + 8), t237);
                        gm_t2ubvi1j1aq22 t239;
                        GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t239 + 0), t238);
                        F.l26 = t239;
                        void * t240 = (void *)((uint8_t *)&F.l26);
                        uint64_t t241 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t242;
                        GM_ST(void *, (uint8_t *)&t242, (void *)(((uint8_t *)t240 + (uintptr_t)((uint64_t)0ULL) * 40)));
                        GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(t241));
                        gm_t88rcucg1b7uk t243 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t235, t236, t242);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l27 = t243;
                        void * t244 = gm_l0;
                        void * t245 = (void *)(((uint8_t *)t244) + 2496);
                        gm_t88rcucg1b7uk t246 = gm_l1;
                        gm_tf5srh4mp4b3i t247 = gm_l2;
                        uint8_t t248 = gm_l5;
                        gm_t88rcucg1b7uk t249 = gm_l27;
                        gm_tcu60n24kv26r t250;
                        GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t250 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t250 + 26, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t250 + 0), t246);
                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t250 + 8), t247);
                        GM_ST(uint8_t, ((uint8_t *)&t250 + 24), t248);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t250 + 28), t249);
                        gm_tftb1odc739t5 t251 = gm_f_push_mf0vgpj(t245, &t250);
                        gm_l44 = t251;
                        uint8_t t252 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                        if (!t252) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_src4nj79mj9jk57, 132, 5);
                        gm_t88rcucg1b7uk t253 = gm_l27;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t253);
                        goto gm_fdone;
                    }
                }
gm_lb11:;
            }
            {
                gm_b16_8 t254 = gm_l6;
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8), t255);
                goto gm_fdone;
            }
        }
gm_lb10:;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_subst_soa_2chqte3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_th3eueiipivq6 gm_l2; /* shape */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* msg */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_te3magq8drarp gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = GM_LD(gm_tf5srh4mp4b3i, ((uint8_t *)a2));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
    gm_f_soa_type_of_q8vtd5j((void *)&gm_l5, (void *)NULL, cx, NULL, t5, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t11;
            gm_t88rcucg1b7uk t12 = gm_l3;
            F.ret = t12;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t15;
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 2576);
            gm_b16_8 t18 = gm_l4;
            gm_tftb1odc739t5 t19 = gm_f_push_3ef746b(t17, t18);
            gm_l6 = t19;
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src4nj79mj9jk57, 182, 13);
            void * t21 = gm_l0;
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t23;
            goto gm_fret;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 179, 5);
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
extern __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);

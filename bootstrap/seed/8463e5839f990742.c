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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnpifv20irnph v0; gm_b16_8 v1; } f1; } gm_ti9v8ce4a1rnh;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti9v8ce4a1rnh) == 24 && _Alignof(gm_ti9v8ce4a1rnh) == 8 && offsetof(gm_ti9v8ce4a1rnh, f0) == 0 && offsetof(gm_ti9v8ce4a1rnh, f1) == 8, "layout of `FormatError!string`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_collect_dir_4paldob {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_ttln0d0hd70en ret;
    gm_b0_1 w0;
};
struct gm_fr_collect_gm_files_8bovlr7 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_command_4iublbb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* paths */
    gm_tjt4ka19str13 l6; /* a */
    gm_trqfvmjtt4sij l7; /* files */
    uint8_t ret;
    gm_b0_1 w1;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_gvn47sj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extension_ljg5l7j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_filter_ud4gt0t {
    gm_frame h;
    gm_tjt4ka19str13 l0; /* a */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_iv0iced {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d801g52 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_file_af2r4jv {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* a */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_qrt4084 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_message_alq5i3u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_collect_dir_4paldob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_collect_gm_files_8bovlr7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_command_4iublbb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s2qjl18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_extension_ljg5l7j(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_filter_ud4gt0t(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_iv0iced(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_format_file_af2r4jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_source_a5lvenl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qrt4084(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_all_lejs9gn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_message_alq5i3u(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cai164r033u[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern uint8_t gm_cv98ob7u5a4[16] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hl4lv3uiadds[];
extern uint8_t gm_kmpad6qi84v[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0116oq3lk1rs[4];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s3hloevtmnfuc[8];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4ehfliahvei9[6];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_saj4ijvbmtohg[9];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sing8pfld8brt[3];
extern const uint8_t gm_sk88l68smd6kt[23];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_smcv6j7b18cjr[17];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sp0p0g46490o0[24];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcml1jlm4abpm3[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplsogg2sp0m9p9;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_gvn47sj F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)16ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_5nn7ph0();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d801g52 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4ehfliahvei9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsogg2sp0m9p9), t2);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_collect_dir_4paldob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_collect_dir_4paldob F;
    gm_b16_8 gm_l0; /* dir */
    void * gm_l1; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint8_t gm_l4; /* package_root */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* name */
    gm_b16_8 gm_l8; /* entry */
    GM_STACK_CHECK(&F, 642u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_ttln0d0hd70en gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_trqfvmjtt4sij t8;
    gm_f_list_epvducg((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t8;
    gm_b16_8 t9 = gm_l0;
    gm_l3 = t9;
    {
        gm_node wn10 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl4lv3uiadds, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t13 = gm_l3;
        gm_b0_1 t14 = gm_f_entries_44o5n9f_s2qjl18((void *)NULL, (&wn10), NULL, t13);
        if (gm_unw.kind) {
            goto gm_ll12;
        }
    }
    goto gm_ls15;
gm_ll12:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we11; }
    goto gm_lad;
gm_ls15:;
gm_lb1:;
we11:;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_f_sort_skeree7(t16);
    gm_b16_8 t17 = gm_l0;
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(10));
    gm_b16_8 t19 = gm_f_join_qrt4084((void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t20;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l5 = t21;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l6;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb2;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t28 = gm_l6;
                {
                    {
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t26) + (uintptr_t)t28 * 16));
                        gm_l7 = t29;
                        gm_b16_8 t30 = gm_l0;
                        gm_b16_8 t31 = gm_l7;
                        gm_b16_8 t32 = gm_f_join_qrt4084((void *)NULL, cx, NULL, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        gm_l8 = t32;
                        gm_b16_8 t33 = gm_l8;
                        uint8_t t34 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t33);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        if (t34) {
                            gm_b16_8 t35 = gm_l7;
                            gm_b16_8 t36;
                            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sl3q52ep6aa53));
                            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
                            uint8_t t37 = gm_f_starts_with_6vvt01e(t35, t36);
                            if (t37) {
                                gm_l9 = ((uint8_t)1);
                            } else {
                                uint8_t t38 = gm_l4;
                                if (t38) {
                                    gm_b16_8 t39 = gm_l7;
                                    gm_b16_8 t40;
                                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_snoai0j6d9u36));
                                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(6));
                                    uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
                                    gm_l10 = t41;
                                } else {
                                    gm_l10 = ((uint8_t)0);
                                }
                                uint8_t t42 = gm_l10;
                                gm_l9 = t42;
                            }
                            uint8_t t43 = gm_l9;
                            if (t43) {
                                goto gm_lb4;
                            }
                            gm_b16_8 t44 = gm_l8;
                            void * t45 = gm_l1;
                            gm_ttln0d0hd70en t46 = gm_f_collect_dir_4paldob((void *)NULL, cx, NULL, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_l11 = t46;
                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (t47) {
                            } else {
                                gm_toe5fmr3vvf4d t48 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l11) + 4));
                                gm_ttln0d0hd70en t49;
                                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                                ((uint8_t *)&t49)[0] = 1;
                                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t49 + 4), t48);
                                F.ret = t49;
                                goto gm_fret;
                            }
                        } else {
                            gm_b16_8 t50 = gm_l7;
                            gm_b16_8 t51 = gm_f_extension_ljg5l7j(t50);
                            gm_b16_8 t52;
                            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sing8pfld8brt));
                            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
                            uint8_t t53 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t52)));
                            if (t53) {
                                void * t54 = gm_l1;
                                gm_b16_8 t55 = gm_l8;
                                gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t54, t55);
                                gm_l12 = t56;
                                uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcml1jlm4abpm3, 133, 13);
                            }
                        }
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 127, 5);
                }
gm_lb5:;
            }
gm_lb4:;
            uint64_t t58 = gm_l6;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l6 = t61;
        }
    }
    goto gm_ls62;
gm_ll22:;
    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb2; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb4; }
    }
    goto gm_lad;
gm_ls62:;
gm_lb2:;
    gm_ttln0d0hd70en t63;
    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t63 + 8, 0);
    ((uint8_t *)&t63)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t63 + 4), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_collect_gm_files_8bovlr7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_collect_gm_files_8bovlr7 F;
    gm_b16_8 gm_l0; /* path */
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tih6h4avv5nrb gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t3) {
        gm_b16_8 t4 = gm_l0;
        void * t5 = gm_l1;
        gm_ttln0d0hd70en t6 = gm_f_collect_dir_4paldob((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t6;
        goto gm_fret;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_node *pn8 = gm_find(cx, 2126215853);
        if (!pn8) gm_trap("no handler installed for effect `Fs`", gm_srcml1jlm4abpm3, 112, 13);
        gm_tih6h4avv5nrb t9;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn8->clauses[5])((void *)&t9, pn8->env, pn8->hctx, pn8, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t9;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t10) {
    } else {
        gm_toe5fmr3vvf4d t11 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l2) + 8));
        gm_ttln0d0hd70en t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        ((uint8_t *)&t12)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 4), t11);
        F.ret = t12;
        goto gm_fret;
    }
    {
    }
    void * t13 = gm_l1;
    gm_b16_8 t14 = gm_l0;
    gm_tftb1odc739t5 t15 = gm_f_push_3ef746b(t13, t14);
    gm_l3 = t15;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcml1jlm4abpm3, 113, 5);
    gm_ttln0d0hd70en t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 8, 0);
    ((uint8_t *)&t17)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t17 + 4), GM_ZERO(gm_b0_1));
    F.ret = t17;
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
__attribute__((aligned(4))) uint8_t gm_f_command_4iublbb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_command_4iublbb F;
    gm_b16_8 gm_l0; /* args */
    uint8_t gm_l1; /* check */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* p */
    gm_toe5fmr3vvf4d gm_l11; /* e */
    uint8_t gm_l12; /* failed */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* f */
    GM_STACK_CHECK(&F, 1060u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l16;
    gm_ttln0d0hd70en gm_l17;
    uint8_t gm_l18;
    {
        gm_l1 = ((uint8_t)0);
        {
            gm_trqfvmjtt4sij t31;
            gm_f_list_epvducg((void *)&t31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t31;
        }
        gm_b16_8 t32 = gm_l0;
        gm_l3 = t32;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t33 = gm_l4;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t35 = (t33 < t34);
                if (!t35) {
                    goto gm_lb2;
                }
                {
                    void * t36 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t38 = gm_l4;
                    {
                        {
                            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t36) + (uintptr_t)t38 * 16));
                            gm_l5 = t39;
                            {
                                {
                                    gm_b16_8 t40 = gm_l5;
                                    gm_b16_8 t41;
                                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s3hloevtmnfuc));
                                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
                                    uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                                    if (!t42) {
                                        goto gm_lb8;
                                    }
                                    gm_l1 = ((uint8_t)1);
                                    goto gm_lb7;
                                }
gm_lb8:;
                                {
                                    {
                                        {
                                            gm_b16_8 t43 = gm_l5;
                                            gm_b16_8 t44;
                                            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s4chbmodjkinv));
                                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
                                            uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
                                            if (!t45) {
                                                goto gm_lb11;
                                            }
                                            goto gm_lb10;
                                        }
gm_lb11:;
                                        {
                                            gm_b16_8 t46 = gm_l5;
                                            gm_b16_8 t47;
                                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s3a1oio07ri1p));
                                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
                                            uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                                            if (!t48) {
                                                goto gm_lb12;
                                            }
                                            goto gm_lb10;
                                        }
gm_lb12:;
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)gm_cv98ob7u5a4));
                                    gm_pfmaojsuds6a(cx, t49, gm_srcml1jlm4abpm3, 19, 17);
                                    if (gm_unw.kind) {
                                        goto gm_ld30_1;
                                    }
                                    F.ret = ((uint8_t)0ULL);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ld30_1;
                                }
gm_lb9:;
                                {
                                    {
                                        {
                                            gm_b16_8 t50 = gm_l5;
                                            gm_b16_8 t51;
                                            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_skolmnfc0ccbd));
                                            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
                                            uint8_t t52 = gm_f_starts_with_6vvt01e(t50, t51);
                                            if (!t52) {
                                                goto gm_lb15;
                                            }
                                            gm_b16_8 t53 = gm_l5;
                                            gm_b16_8 t54;
                                            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_skolmnfc0ccbd));
                                            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
                                            uint8_t t55 = (!gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                                            if (!t55) {
                                                goto gm_lb15;
                                            }
                                            gm_b16_8 t56 = gm_l5;
                                            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)gm_cv98ob7u5a4));
                                            if (gm_pf454qfrpt6r(cx, gm_sp0p0g46490o0, 23, gm_srcml1jlm4abpm3, 24, 21)) {
                                                goto gm_ld30_1;
                                            }
                                            if ((GM_LD(uintptr_t, ((uint8_t *)&t56) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t56)), GM_LD(uintptr_t, ((uint8_t *)&t56) + 8), gm_srcml1jlm4abpm3, 24, 21))) {
                                                goto gm_ld30_1;
                                            }
                                            if (gm_pf454qfrpt6r(cx, gm_s0116oq3lk1rs, 3, gm_srcml1jlm4abpm3, 24, 21)) {
                                                goto gm_ld30_1;
                                            }
                                            if ((GM_LD(uintptr_t, ((uint8_t *)&t57) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t57)), GM_LD(uintptr_t, ((uint8_t *)&t57) + 8), gm_srcml1jlm4abpm3, 24, 21))) {
                                                goto gm_ld30_1;
                                            }
                                            F.ret = ((uint8_t)2ULL);
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                            goto gm_ld30_1;
                                        }
gm_lb15:;
                                    }
gm_lb14:;
                                    void * t58 = (void *)((uint8_t *)&F.l2);
                                    gm_b16_8 t59 = gm_l5;
                                    gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t58, t59);
                                    gm_l16 = t60;
                                    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcml1jlm4abpm3, 27, 17);
                                    goto gm_lb7;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 16, 9);
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 15, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t62 = gm_l4;
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                uint64_t t65 = r63;
                gm_l4 = t65;
            }
        }
gm_lb2:;
        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t67 = ((t66 == ((uint64_t)0ULL)));
        if (t67) {
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)gm_cv98ob7u5a4));
            if (gm_pf454qfrpt6r(cx, gm_sk88l68smd6kt, 22, gm_srcml1jlm4abpm3, 32, 9)) {
                goto gm_ld30_1;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t68) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t68)), GM_LD(uintptr_t, ((uint8_t *)&t68) + 8), gm_srcml1jlm4abpm3, 32, 9))) {
                goto gm_ld30_1;
            }
            F.ret = ((uint8_t)2ULL);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld30_1;
        }
        {
            {
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                uint8_t t70 = ((t69 == ((uint64_t)1ULL)));
                if (!t70) {
                    goto gm_lb17;
                }
                void * t71 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t72, gm_srcml1jlm4abpm3, 35, 33);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)t71) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_b16_8 t74;
                GM_ST(void *, (uint8_t *)&t74, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
                uint8_t t75 = (gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
                if (!t75) {
                    goto gm_lb17;
                }
                uint8_t t76 = gm_f_filter_ud4gt0t((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ld30_1;
                }
                F.ret = t76;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld30_1;
            }
gm_lb17:;
        }
gm_lb16:;
        {
            gm_tjt4ka19str13 t77;
            gm_f_new_jr5ejs0((void *)&t77, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld30_1;
            }
            F.l6 = t77;
        }
        {
            void * t78 = (void *)((uint8_t *)&F.l6);
            gm_t3mf58gv9kf3n t79 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t78));
            gm_t3mf58gv9kf3n t80 = t79;
            gm_node wn81 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t80, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
            gm_trqfvmjtt4sij t84;
            gm_f_list_epvducg((void *)&t84, (void *)NULL, (&wn81), NULL);
            if (gm_unw.kind) {
                goto gm_ll83;
            }
            F.l7 = t84;
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_l8 = t85;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t86 = gm_l9;
                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t88 = (t86 < t87);
                    if (!t88) {
                        goto gm_lb19;
                    }
                    {
                        void * t89 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t91 = gm_l9;
                        {
                            {
                                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t89) + (uintptr_t)t91 * 16));
                                gm_l10 = t92;
                                gm_b16_8 t93 = gm_l10;
                                void * t94 = (void *)((uint8_t *)&F.l7);
                                gm_ttln0d0hd70en t95 = gm_f_collect_gm_files_8bovlr7((void *)NULL, (&wn81), NULL, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_ll83;
                                }
                                gm_l17 = t95;
                                uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (t96) {
                                } else {
                                    gm_toe5fmr3vvf4d t97 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l17) + 4));
                                    gm_l11 = t97;
                                    gm_b16_8 t98 = gm_l10;
                                    gm_toe5fmr3vvf4d t99 = gm_l11;
                                    gm_b16_8 t100 = gm_f_os_error_text_319eh6h((void *)NULL, (&wn81), NULL, t99);
                                    if (gm_unw.kind) {
                                        goto gm_ll83;
                                    }
                                    if (gm_pf454qfrpt6r((&wn81), gm_s5lqhiu2bn6of, 7, gm_srcml1jlm4abpm3, 42, 17)) {
                                        goto gm_ll83;
                                    }
                                    if ((GM_LD(uintptr_t, ((uint8_t *)&t98) + 8) != 0 && gm_pf454qfrpt6r((&wn81), GM_LD(void *, ((uint8_t *)&t98)), GM_LD(uintptr_t, ((uint8_t *)&t98) + 8), gm_srcml1jlm4abpm3, 42, 17))) {
                                        goto gm_ll83;
                                    }
                                    if (gm_pf454qfrpt6r((&wn81), gm_slgmqgkioa4u6, 2, gm_srcml1jlm4abpm3, 42, 17)) {
                                        goto gm_ll83;
                                    }
                                    if ((GM_LD(uintptr_t, ((uint8_t *)&t100) + 8) != 0 && gm_pf454qfrpt6r((&wn81), GM_LD(void *, ((uint8_t *)&t100)), GM_LD(uintptr_t, ((uint8_t *)&t100) + 8), gm_srcml1jlm4abpm3, 42, 17))) {
                                        goto gm_ll83;
                                    }
                                    if (gm_pf454qfrpt6r((&wn81), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 42, 17)) {
                                        goto gm_ll83;
                                    }
                                    F.ret = ((uint8_t)1ULL);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ll83;
                                }
                                goto gm_lb22;
                            }
gm_lb23:;
                            gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 40, 9);
                        }
gm_lb22:;
                    }
gm_lb21:;
                    uint64_t t101 = gm_l9;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l9 = t104;
                }
            }
gm_lb19:;
            gm_l12 = ((uint8_t)0);
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_l13 = t105;
            gm_l14 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t106 = gm_l14;
                    uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint8_t t108 = (t106 < t107);
                    if (!t108) {
                        goto gm_lb24;
                    }
                    {
                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t111 = gm_l14;
                        {
                            {
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)t109) + (uintptr_t)t111 * 16));
                                gm_l15 = t112;
                                gm_b16_8 t113 = gm_l15;
                                uint8_t t114 = gm_l1;
                                uint8_t t115 = gm_f_format_file_af2r4jv((void *)NULL, (&wn81), NULL, t113, t114);
                                if (gm_unw.kind) {
                                    goto gm_ll83;
                                }
                                uint8_t t116 = (uint8_t)!t115;
                                if (t116) {
                                    gm_l12 = ((uint8_t)1);
                                }
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 47, 9);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t117 = gm_l14;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l14 = t120;
                }
            }
gm_lb24:;
            uint8_t t121 = gm_l12;
            if (t121) {
                gm_l18 = ((uint8_t)1ULL);
            } else {
                gm_l18 = ((uint8_t)0ULL);
            }
            uint8_t t122 = gm_l18;
            F.ret = t122;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ll83;
        }
        goto gm_ls123;
gm_ll83:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we82; }
        goto gm_ld30_2;
gm_ls123:;
gm_lb18:;
we82:;
    }
    {
        void * t124 = (void *)((uint8_t *)&F.l6);
        gm_f_deinit_sejdpqv(t124);
    }
    {
        void * t125 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t125);
    }
    goto gm_ls126;
gm_ld30_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t127 = (void *)((uint8_t *)&F.l6);
        gm_f_deinit_sejdpqv(t127);
        gm_unw = gm_saved;
    }
gm_ld30_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t128 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t128);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls126:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_extension_ljg5l7j(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extension_ljg5l7j F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t5;
    {
        for (;;) {
            uint64_t t6 = gm_l1;
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            if (!t7) {
                goto gm_lb1;
            }
            {
                uint64_t t8 = gm_l1;
                uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
                if (o10) gm_trap("integer overflow in `-`", gm_srcml1jlm4abpm3, 143, 9);
                uint64_t t11 = r9;
                gm_l1 = t11;
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l1;
                if ((uintptr_t)t14 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t14, t13, gm_srcml1jlm4abpm3, 144, 12);
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)t14 * 1));
                uint8_t t16 = ((t15 == ((uint8_t)46ULL)));
                if (t16) {
                    uint64_t t17 = gm_l1;
                    uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
                    if (t18) {
                        gm_b16_8 t19;
                        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
                        F.ret = t19;
                        goto gm_fret;
                    }
                    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t22 = gm_l1;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    if (o24) gm_trap("integer overflow in `+`", gm_srcml1jlm4abpm3, 146, 25);
                    uint64_t t25 = r23;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = (uintptr_t)t21;
                    if (t26 > t27 || t27 > (uintptr_t)t21) gm_trap_slice(t26, t27, t21, gm_srcml1jlm4abpm3, 146, 20);
                    uint64_t t28 = ((uintptr_t)t21) - (uintptr_t)t25;
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t20 + (uintptr_t)t25 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28));
                    F.ret = t29;
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(0));
    F.ret = t30;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_filter_ud4gt0t(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_filter_ud4gt0t F;
    gm_tj5jn79kq9pnm gm_l1; /* e */
    gm_b16_8 gm_l2; /* data */
    gm_tnpifv20irnph gm_l3; /* e */
    gm_b16_8 gm_l4; /* out */
    GM_STACK_CHECK(&F, 418u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tgvh15qshf643 gm_l5;
    gm_ti9v8ce4a1rnh gm_l6;
    {
        gm_tjt4ka19str13 t5;
        gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l0 = t5;
        {
            void * t6 = (void *)((uint8_t *)&F.l0);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            {
                gm_f_read_all_lejs9gn((void *)&gm_l5, (void *)NULL, (&wn9), NULL);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
            }
            uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t12) {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_l2 = t13;
            } else {
                gm_tj5jn79kq9pnm t14 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l5) + 8));
                gm_l1 = t14;
                gm_tj5jn79kq9pnm t15 = gm_l1;
                gm_b16_8 t16 = gm_f_read_message_alq5i3u((void *)NULL, (&wn9), NULL, t15);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_smcv6j7b18cjr, 16, gm_srcml1jlm4abpm3, 60, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8), gm_srcml1jlm4abpm3, 60, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 60, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)1ULL);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            gm_b16_8 t17 = gm_l2;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            uint8_t t19 = gm_f_utf8_valid_a8oc1t8(t18);
            uint8_t t20 = (uint8_t)!t19;
            if (t20) {
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)gm_cai164r033u));
                if (gm_pf454qfrpt6r((&wn9), gm_smcv6j7b18cjr, 16, gm_srcml1jlm4abpm3, 64, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t21) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t21)), GM_LD(uintptr_t, ((uint8_t *)&t21) + 8), gm_srcml1jlm4abpm3, 64, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 64, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)1ULL);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            {
                gm_b16_8 t22 = gm_l2;
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                gm_f_format_source_a5lvenl((void *)&gm_l6, (void *)NULL, (&wn9), NULL, t23);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
            }
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (t24) {
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_l4 = t25;
            } else {
                gm_tnpifv20irnph t26 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l6) + 8));
                gm_l3 = t26;
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                if (gm_pf454qfrpt6r((&wn9), gm_saj4ijvbmtohg, 8, gm_srcml1jlm4abpm3, 68, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t27) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t27)), GM_LD(uintptr_t, ((uint8_t *)&t27) + 8), gm_srcml1jlm4abpm3, 68, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 68, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)1ULL);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            gm_b16_8 t28 = gm_l4;
            gm_pfmaojsuds6a((&wn9), t28, gm_srcml1jlm4abpm3, 71, 9);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            F.ret = ((uint8_t)0ULL);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ll11;
        }
        goto gm_ls29;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls29:;
gm_lb2:;
we10:;
    }
    {
        void * t30 = (void *)((uint8_t *)&F.l0);
        gm_f_deinit_sejdpqv(t30);
    }
    goto gm_ls31;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t32 = (void *)((uint8_t *)&F.l0);
        gm_f_deinit_sejdpqv(t32);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls31:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_iv0iced(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_iv0iced F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* copy */
    GM_STACK_CHECK(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tabb710d9mq2e gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            }
            uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t4) {
            } else {
                gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l1) + 8));
                gm_ttln0d0hd70en t6;
                GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
                ((uint8_t *)&t6)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t6 + 4), t5);
                uint8_t t7 = ((((uint8_t *)&t6))[0] != 0);
                ((struct gm_fr_collect_dir_4paldob *)((&F.h)->up))->ret = t6;
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t7;
                goto gm_lad;
            }
            {
                memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l1) + 8), 24);
            }
            {
                uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                gm_f_make_2ip78bj((void *)&gm_l4, (void *)NULL, cx, NULL, t8, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmpad6qi84v)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcml1jlm4abpm3, 121, 17);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_l3 = t10;
            gm_b16_8 t11 = gm_l3;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_f_copy_from_f7j6p05(t11, t12);
            void * t13 = (void *)((uint8_t *)&((struct gm_fr_collect_dir_4paldob *)((&F.h)->up))->l2);
            gm_b16_8 t14 = gm_l3;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_tftb1odc739t5 t16 = gm_f_push_3ef746b(t13, t15);
            gm_l5 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcml1jlm4abpm3, 123, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 119, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_format_file_af2r4jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_format_file_af2r4jv F;
    gm_b16_8 gm_l0; /* path */
    uint8_t gm_l1; /* check */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* src */
    gm_tnpifv20irnph gm_l5; /* e */
    gm_b16_8 gm_l6; /* out */
    gm_toe5fmr3vvf4d gm_l7; /* e */
    GM_STACK_CHECK(&F, 662u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tgvh15qshf643 gm_l8;
    gm_ti9v8ce4a1rnh gm_l9;
    gm_ttln0d0hd70en gm_l10;
    {
        gm_tjt4ka19str13 t5;
        gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t5;
        {
            void * t6 = (void *)((uint8_t *)&F.l2);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            {
                gm_b16_8 t12 = gm_l0;
                gm_f_read_all_r9rh9qd((void *)&gm_l8, (void *)NULL, (&wn9), NULL, t12);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
            }
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (t13) {
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                gm_l4 = t14;
            } else {
                gm_tj5jn79kq9pnm t15 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l8) + 8));
                gm_l3 = t15;
                gm_b16_8 t16 = gm_l0;
                gm_tj5jn79kq9pnm t17 = gm_l3;
                gm_b16_8 t18 = gm_f_read_message_alq5i3u((void *)NULL, (&wn9), NULL, t17);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_s5lqhiu2bn6of, 7, gm_srcml1jlm4abpm3, 83, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8), gm_srcml1jlm4abpm3, 83, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_slgmqgkioa4u6, 2, gm_srcml1jlm4abpm3, 83, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t18)), GM_LD(uintptr_t, ((uint8_t *)&t18) + 8), gm_srcml1jlm4abpm3, 83, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 83, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            gm_b16_8 t19 = gm_l4;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            uint8_t t21 = gm_f_utf8_valid_a8oc1t8(t20);
            uint8_t t22 = (uint8_t)!t21;
            if (t22) {
                gm_b16_8 t23 = gm_l0;
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)gm_cai164r033u));
                if (gm_pf454qfrpt6r((&wn9), gm_s5lqhiu2bn6of, 7, gm_srcml1jlm4abpm3, 87, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t23) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t23)), GM_LD(uintptr_t, ((uint8_t *)&t23) + 8), gm_srcml1jlm4abpm3, 87, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_slgmqgkioa4u6, 2, gm_srcml1jlm4abpm3, 87, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t24) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t24)), GM_LD(uintptr_t, ((uint8_t *)&t24) + 8), gm_srcml1jlm4abpm3, 87, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 87, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            {
                gm_b16_8 t25 = gm_l4;
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                gm_f_format_source_a5lvenl((void *)&gm_l9, (void *)NULL, (&wn9), NULL, t26);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
            }
            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (t27) {
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_l6 = t28;
            } else {
                gm_tnpifv20irnph t29 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l9) + 8));
                gm_l5 = t29;
                gm_b16_8 t30 = gm_l0;
                gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                if ((GM_LD(uintptr_t, ((uint8_t *)&t30) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t30)), GM_LD(uintptr_t, ((uint8_t *)&t30) + 8), gm_srcml1jlm4abpm3, 91, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_s39mljh669qgl, 1, gm_srcml1jlm4abpm3, 91, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t31) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t31)), GM_LD(uintptr_t, ((uint8_t *)&t31) + 8), gm_srcml1jlm4abpm3, 91, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 91, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            gm_b16_8 t32 = gm_l6;
            gm_b16_8 t33 = gm_l4;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t34)));
            if (t35) {
                F.ret = ((uint8_t)1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            uint8_t t36 = gm_l1;
            if (t36) {
                gm_b16_8 t37 = gm_l0;
                if ((GM_LD(uintptr_t, ((uint8_t *)&t37) + 8) != 0 && gm_pf1m4b8q4e78((&wn9), GM_LD(void *, ((uint8_t *)&t37)), GM_LD(uintptr_t, ((uint8_t *)&t37) + 8), gm_srcml1jlm4abpm3, 96, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf1m4b8q4e78((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 96, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            gm_b16_8 t38 = gm_l0;
            gm_b16_8 t39 = gm_l6;
            gm_ttln0d0hd70en t40 = gm_f_write_all_o8ucaji((void *)NULL, (&wn9), NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_l10 = t40;
            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (t41) {
            } else {
                gm_toe5fmr3vvf4d t42 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l10) + 4));
                gm_l7 = t42;
                gm_b16_8 t43 = gm_l0;
                gm_toe5fmr3vvf4d t44 = gm_l7;
                gm_b16_8 t45 = gm_f_os_error_text_319eh6h((void *)NULL, (&wn9), NULL, t44);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_s5lqhiu2bn6of, 7, gm_srcml1jlm4abpm3, 100, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t43) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t43)), GM_LD(uintptr_t, ((uint8_t *)&t43) + 8), gm_srcml1jlm4abpm3, 100, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_slgmqgkioa4u6, 2, gm_srcml1jlm4abpm3, 100, 13)) {
                    goto gm_ll11;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t45) + 8) != 0 && gm_pf454qfrpt6r((&wn9), GM_LD(void *, ((uint8_t *)&t45)), GM_LD(uintptr_t, ((uint8_t *)&t45) + 8), gm_srcml1jlm4abpm3, 100, 13))) {
                    goto gm_ll11;
                }
                if (gm_pf454qfrpt6r((&wn9), gm_so514oj5nbhcu, 1, gm_srcml1jlm4abpm3, 100, 13)) {
                    goto gm_ll11;
                }
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ll11;
            }
            F.ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ll11;
        }
        goto gm_ls46;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls46:;
gm_lb2:;
we10:;
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t47);
    }
    goto gm_ls48;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t49 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t49);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls48:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_qrt4084(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_qrt4084 F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t4 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t r7; bool o8 = __builtin_sub_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `-`", gm_srcml1jlm4abpm3, 159, 32);
        uint64_t t9 = r7;
        if ((uintptr_t)t9 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t9, t5, gm_srcml1jlm4abpm3, 159, 28);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t4) + (uintptr_t)t9 * 1));
        uint8_t t11 = ((t10 == ((uint8_t)47ULL)));
        gm_l2 = t11;
    }
    uint8_t t12 = gm_l2;
    if (t12) {
        {
            gm_b16_8 t13 = gm_l0;
            gm_b16_8 t14 = gm_l1;
            gm_t6j6smj7qoctu t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t13);
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 16), t14);
            gm_f_format_8n626pi((void *)&gm_l3, (void *)NULL, cx, NULL, &t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcml1jlm4abpm3, 159, 61);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        F.ret = t18;
        goto gm_fret;
    }
    {
        gm_b16_8 t19 = gm_l0;
        gm_b16_8 t20 = gm_l1;
        gm_t6j6smj7qoctu t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 16), t20);
        gm_f_format_d801g52((void *)&gm_l4, (void *)NULL, cx, NULL, &t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcml1jlm4abpm3, 160, 16);
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    F.ret = t24;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_message_alq5i3u(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_message_alq5i3u F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io_err */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t7;
            gm_toe5fmr3vvf4d t8 = gm_l1;
            gm_b16_8 t9 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sidpdvffdra02));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(13));
            gm_l2 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcml1jlm4abpm3, 166, 62);
    }
gm_lb1:;
    gm_b16_8 t13 = gm_l2;
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

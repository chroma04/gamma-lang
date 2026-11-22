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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { __int128 f0; __int128 f1; uint64_t f2; uint8_t pe[8]; } gm_tsnr5s2o1o6ck;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; int64_t f2; } gm_t2u4qamcbbgu0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { gm_t9qkh0rsooc8d f0; } gm_tjj2cncin95t1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tl4mjim8icrii;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsnr5s2o1o6ck) == 48 && _Alignof(gm_tsnr5s2o1o6ck) == 16 && offsetof(gm_tsnr5s2o1o6ck, f0) == 0 && offsetof(gm_tsnr5s2o1o6ck, f1) == 16 && offsetof(gm_tsnr5s2o1o6ck, f2) == 32, "layout of `(i128, i128, u64)`");
typedef struct { int32_t f0; uint8_t p1[4]; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_ttld71ltp94ka;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tu8mt0h8clq1l;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t2u4qamcbbgu0) == 32 && _Alignof(gm_t2u4qamcbbgu0) == 8 && offsetof(gm_t2u4qamcbbgu0, f0) == 0 && offsetof(gm_t2u4qamcbbgu0, f1) == 16 && offsetof(gm_t2u4qamcbbgu0, f2) == 24, "layout of `(string, u64, i64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
typedef struct { gm_ts6oqppjh0ba3 f0; } gm_t6d4s85lju6dd;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tjj2cncin95t1) == 1 && _Alignof(gm_tjj2cncin95t1) == 1 && offsetof(gm_tjj2cncin95t1, f0) == 0, "layout of `(Wrap,)`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tl4mjim8icrii) == 56 && _Alignof(gm_tl4mjim8icrii) == 8 && offsetof(gm_tl4mjim8icrii, f0) == 0 && offsetof(gm_tl4mjim8icrii, f1) == 8 && offsetof(gm_tl4mjim8icrii, f2) == 24 && offsetof(gm_tl4mjim8icrii, f3) == 40, "layout of `(u32, string, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ttld71ltp94ka) == 40 && _Alignof(gm_ttld71ltp94ka) == 8 && offsetof(gm_ttld71ltp94ka, f0) == 0 && offsetof(gm_ttld71ltp94ka, f1) == 8 && offsetof(gm_ttld71ltp94ka, f2) == 16 && offsetof(gm_ttld71ltp94ka, f3) == 32, "layout of `(i32, u64, string, u64)`");
_Static_assert(sizeof(gm_tu8mt0h8clq1l) == 56 && _Alignof(gm_tu8mt0h8clq1l) == 8 && offsetof(gm_tu8mt0h8clq1l, f0) == 0 && offsetof(gm_tu8mt0h8clq1l, f1) == 16 && offsetof(gm_tu8mt0h8clq1l, f2) == 32 && offsetof(gm_tu8mt0h8clq1l, f3) == 40, "layout of `(string, string, u32, string)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t6d4s85lju6dd) == 2 && _Alignof(gm_t6d4s85lju6dd) == 1 && offsetof(gm_t6d4s85lju6dd, f0) == 0, "layout of `(BinOp,)`");
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_64itgp2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9e2ubbk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9v60a6q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cdr8adu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ds16s19 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eph0ild {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hiq3u0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nn7to1o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ulerodq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_a07tfib {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a09tbhs {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a14ppi0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1ch927 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1ec0ji {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1j85ht {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a2pi3bt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a4nv9mc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a4r7k06 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a4vi4f7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a5p8et1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a612q5e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a7bth9i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a834m1a {
    gm_frame h;
    gm_tl4mjim8icrii l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tl4mjim8icrii l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aa62g71 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_abb68p1 {
    gm_frame h;
    gm_thognv9odov03 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_abcvqrv {
    gm_frame h;
    gm_tk6rvhh54g86p l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tk6rvhh54g86p l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aculdom {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adbd97i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adcs7to {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adtncrl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aeib2bc {
    gm_frame h;
    gm_tsnr5s2o1o6ck l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tsnr5s2o1o6ck l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_afagrs5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_afan86j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_afn1q7g {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ag0n4rn {
    gm_frame h;
    gm_tu8mt0h8clq1l l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tu8mt0h8clq1l l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aghnr00 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_agi9f7f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ahg4b8c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ahgk48j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ahnpa17 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aib5qn5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aiomp5b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ajecpf6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ajjbrpr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_akc2snl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_akfobpm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_amt7kei {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ane7f49 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_anrgoqv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aome4un {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aoqs6rj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aovijlt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_apg2igl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_apgk46s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aq84o6u {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aqb9tiu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aqpcar1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_arnesr1 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_arud364 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_as0b1t3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_asjuqmr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_askmr98 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_atch2pp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_atintr3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_avhd7c9 {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_avnkm9g {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_avo85vj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q07lmui {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q0doeth {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q190eul {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q296tqt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q2e4fnf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q2omjae {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q2uenf3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q32bjcv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q33vns6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q36d130 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q3eb2dn {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q3io8lg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q451tst {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q4qso5a {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q62qvnl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q6prg09 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q6qvtu0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8f0n70 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8i7gsb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8r21un {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8uj6ou {
    gm_frame h;
    gm_temv198mna2jp l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qaapvnn {
    gm_frame h;
    gm_ttld71ltp94ka l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qc4s0m5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qcp7nf9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qds4t66 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qe2mgnq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qe94i5r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qfgmn9b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qh5v7ts {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qhad3qp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qilugr5 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qjdh7n5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qjfu8g6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qk20moi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ql3u2no {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qm09p7m {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qmamljh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qner803 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qofaffd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qon8vgm {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qpj4o1j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qpmmpss {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qqtqagv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qr5c6un {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qskma1s {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qu5usm7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qu7vf4f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qv493su {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qvc703s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qvsrn69 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reserve_d1qo8mf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64itgp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9e2ubbk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9v60a6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cdr8adu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ds16s19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eph0ild(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hiq3u0r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nn7to1o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulerodq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_0dva8tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_13o7g7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_1o6fs38(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2bh0ost(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_6hrn7j8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_817tni2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_92fech4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a4nv9mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a612q5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a834m1a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_aa62g71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_abb68p1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_abcvqrv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aeib2bc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_afan86j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_afn1q7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ag0n4rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ahg4b8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_ajecpf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_akc2snl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_akfobpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ane7f49(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_anrgoqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_aoqs6rj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aovijlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_apg2igl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_aqpcar1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_arud364(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_asjuqmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_avnkm9g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_avo85vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t6d4s85lju6dd a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_bqgl4hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_cp3hiv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_h12an8k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_he6jnp0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ln6s15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_p95juln(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_peglcgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q0doeth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_q190eul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q2e4fnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q2omjae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q33vns6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q36d130(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q3eb2dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q4qso5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q62qvnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q6qvtu0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q8uj6ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qaapvnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qfgmn9b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qh5v7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qilugr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ql3u2no(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qner803(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qon8vgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_qpj4o1j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qqtqagv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_qskma1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qu7vf4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qvc703s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qvsrn69(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rjuhqv1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_t22qvc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s0963m82avtn1[23];
extern const uint8_t gm_s121p29851qjl[39];
extern const uint8_t gm_s129prqlnb2kt[41];
extern const uint8_t gm_s1cbu3jc19u4t[36];
extern const uint8_t gm_s1h7h9updp99l[54];
extern const uint8_t gm_s1h9rpfuhvh29[60];
extern const uint8_t gm_s1jim1okq5o2b[192];
extern const uint8_t gm_s20mq4dk47bdd[4];
extern const uint8_t gm_s27tmg3h56aru[53];
extern const uint8_t gm_s2bumqed39p41[17];
extern const uint8_t gm_s2c24uim36k06[36];
extern const uint8_t gm_s2m3pa8n4g75s[10];
extern const uint8_t gm_s3k1sno8a2ipv[70];
extern const uint8_t gm_s3kfpoo7v2f2o[27];
extern const uint8_t gm_s3m5ql5voef28[6];
extern const uint8_t gm_s3o2km4i906va[32];
extern const uint8_t gm_s3udkpu8iv0h2[75];
extern const uint8_t gm_s4cdme6b7eogn[56];
extern const uint8_t gm_s4mij084uljjf[26];
extern const uint8_t gm_s5dln0neuav6j[11];
extern const uint8_t gm_s5j95frjqt5af[8];
extern const uint8_t gm_s5url3si4frr2[69];
extern const uint8_t gm_s626miklr3ggr[34];
extern const uint8_t gm_s6dtn4tje3j7q[9];
extern const uint8_t gm_s6nbs6o2qkgvg[16];
extern const uint8_t gm_s6sdj2gjsdah5[18];
extern const uint8_t gm_s87c4jti7ujk5[31];
extern const uint8_t gm_s8rv63m5o5imh[9];
extern const uint8_t gm_s8u62e7lr2m4s[13];
extern const uint8_t gm_s95g98ft0cnt1[155];
extern const uint8_t gm_sa92qppdjht4t[100];
extern const uint8_t gm_sasnane8pan26[33];
extern const uint8_t gm_sbbre965bpo5f[77];
extern const uint8_t gm_sbim95dtkmqmv[49];
extern const uint8_t gm_sc22qkqa9q462[16];
extern const uint8_t gm_sc6uqn3rvm1p8[56];
extern const uint8_t gm_sc96am1bplmoh[80];
extern const uint8_t gm_scommn379heqq[14];
extern const uint8_t gm_sd1tvohjm34vg[37];
extern const uint8_t gm_sdcigk5ajbcfi[68];
extern const uint8_t gm_sdm98dp52j8ep[16];
extern const uint8_t gm_se5118fq98e06[88];
extern const uint8_t gm_sejbaeqb5aort[57];
extern const uint8_t gm_sevm6b5otfta5[21];
extern const uint8_t gm_sf3uq7o37v8ks[13];
extern const uint8_t gm_sg39dr0kaebm0[10];
extern const uint8_t gm_sg4pgef1hupjq[45];
extern const uint8_t gm_sgcf9cdlaf08n[6];
extern const uint8_t gm_sgq6hvrcda4sp[37];
extern const uint8_t gm_sh3r0ji6563do[127];
extern const uint8_t gm_sh8j22p8lpudd[15];
extern const uint8_t gm_shhak6l2rrn43[15];
extern const uint8_t gm_shqvr1vn78f0b[54];
extern const uint8_t gm_shror0skne60i[45];
extern const uint8_t gm_si5knpkeaqca7[137];
extern const uint8_t gm_sims8gu714a9a[14];
extern const uint8_t gm_sisd1n433forg[52];
extern const uint8_t gm_sj64h79nh4kvi[57];
extern const uint8_t gm_sjeoqheucmn3m[19];
extern const uint8_t gm_sk4tdtvin0g62[31];
extern const uint8_t gm_skdhvc36gfkb0[47];
extern const uint8_t gm_skmsgkj4jeo7n[6];
extern const uint8_t gm_sks5ueb3h9e10[24];
extern const uint8_t gm_skucss3rkslf9[57];
extern const uint8_t gm_slbn3chj3p1e1[49];
extern const uint8_t gm_slkoo2ifkem47[62];
extern const uint8_t gm_slnlsebkdpqrk[9];
extern const uint8_t gm_slpi6dabivg2e[29];
extern const uint8_t gm_sltvces0c7l80[14];
extern const uint8_t gm_sm30gjvs8dlav[7];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smpdttgra9a1b[85];
extern const uint8_t gm_sn06hdjl3nknj[87];
extern const uint8_t gm_sn0il8abv9oq3[8];
extern const uint8_t gm_snpp7pgdikfns[10];
extern const uint8_t gm_solguqm0afvc4[44];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const uint8_t gm_sp7619gun38fi[63];
extern const uint8_t gm_spc8ncjo0q4pi[11];
extern const uint8_t gm_sps1dthki4jpu[50];
extern const uint8_t gm_sqmd0r48ameas[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srhrn3fa70ni4[38];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_ssld67umkdenq[20];
extern const uint8_t gm_ssp208k4afn15[23];
extern const uint8_t gm_stj45lnsahel3[30];
extern const uint8_t gm_stm1tprvbfoip[58];
extern const uint8_t gm_sua99rsdk6d1r[51];
extern const uint8_t gm_sue9kqmk98bh9[34];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_sumd85rpgjjrv[9];
extern const uint8_t gm_summega2vonc9[6];
extern const uint8_t gm_suntqnp3vfhar[16];
extern const uint8_t gm_supnh5d0f56vu[8];
extern const uint8_t gm_surt6bemh510v[31];
extern const uint8_t gm_suskhmu5llblc[67];
extern const uint8_t gm_suvh4o70k4cg3[28];
extern const uint8_t gm_svgaurqm99100[48];
extern const uint8_t gm_svi913vrigpr0[107];
extern const gm_tmpl gm_tpl00rv7vuig9ur;
extern const gm_tmpl gm_tpl051t251mssfu;
extern const gm_tmpl gm_tpl09kglfpefe8i;
extern const gm_tmpl gm_tpl0epjr4745p10;
extern const gm_tmpl gm_tpl0o80ilivc5vg;
extern const gm_tmpl gm_tpl1ige45obm306;
extern const gm_tmpl gm_tpl1jgrbdjt2sm3;
extern const gm_tmpl gm_tpl1u3j3qlmtjj2;
extern const gm_tmpl gm_tpl20t94jlvdndv;
extern const gm_tmpl gm_tpl2a8tmpvk11vt;
extern const gm_tmpl gm_tpl2fb8qlpeqlet;
extern const gm_tmpl gm_tpl2r84u6vnc58k;
extern const gm_tmpl gm_tpl2u97uspakqd9;
extern const gm_tmpl gm_tpl34lim186g0fm;
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl606t7k5n88ss;
extern const gm_tmpl gm_tpl64ltc7suc5qg;
extern const gm_tmpl gm_tpl6ilgcn878l74;
extern const gm_tmpl gm_tpl6rsespo9a6nq;
extern const gm_tmpl gm_tpl7eehu7c1eqka;
extern const gm_tmpl gm_tpl91b7sjgnmine;
extern const gm_tmpl gm_tpl97n87h3pcva1;
extern const gm_tmpl gm_tpl9gmvk2gm4l5h;
extern const gm_tmpl gm_tpl9kqhb5r0uube;
extern const gm_tmpl gm_tplae6k9mv0s1dm;
extern const gm_tmpl gm_tplaoj5r4tauejk;
extern const gm_tmpl gm_tplba0kefu5gu2c;
extern const gm_tmpl gm_tplbn5t5stpvru2;
extern const gm_tmpl gm_tplbtqgdimqhkr1;
extern const gm_tmpl gm_tplc0jhs849b438;
extern const gm_tmpl gm_tplcef5l6cvp5lt;
extern const gm_tmpl gm_tplcq107msa8ktd;
extern const gm_tmpl gm_tplcudcaj1iluqd;
extern const gm_tmpl gm_tpld4u3te7g9c5v;
extern const gm_tmpl gm_tpldc3nmdc0cit1;
extern const gm_tmpl gm_tpldg1m204qb08c;
extern const gm_tmpl gm_tple4r1aumd134l;
extern const gm_tmpl gm_tple59d4hgjgp3n;
extern const gm_tmpl gm_tple75bobdc6q0g;
extern const gm_tmpl gm_tpleiompgmn2730;
extern const gm_tmpl gm_tplellk9btkq1f2;
extern const gm_tmpl gm_tplem02qig8laoi;
extern const gm_tmpl gm_tpleonu21sc9fi7;
extern const gm_tmpl gm_tplf28d52hmmd2n;
extern const gm_tmpl gm_tplfmqnc0q3nc41;
extern const gm_tmpl gm_tplg4866gcgr837;
extern const gm_tmpl gm_tplgr8ep9alj0ph;
extern const gm_tmpl gm_tplh6c513o0038j;
extern const gm_tmpl gm_tplho1kg74nlm64;
extern const gm_tmpl gm_tplhp4k6h527j3k;
extern const gm_tmpl gm_tplhr6emralh8q6;
extern const gm_tmpl gm_tplhsmbcv8t80v3;
extern const gm_tmpl gm_tpli9uflldtkfq3;
extern const gm_tmpl gm_tplie81jau63n6g;
extern const gm_tmpl gm_tplj57b11lvidge;
extern const gm_tmpl gm_tplj6imuu9dopvn;
extern const gm_tmpl gm_tplj9vh1qm5m031;
extern const gm_tmpl gm_tpljm6mn1j3nrs6;
extern const gm_tmpl gm_tplk5t5jhqqvnff;
extern const gm_tmpl gm_tplkh4k0ikfsmp6;
extern const gm_tmpl gm_tplkpqhnfan9vel;
extern const gm_tmpl gm_tplld9eu0oenqb7;
extern const gm_tmpl gm_tpllfhth9mee4q8;
extern const gm_tmpl gm_tpllktp07diubcq;
extern const gm_tmpl gm_tplmf8455dp92a3;
extern const gm_tmpl gm_tpln3j86kdpqd7a;
extern const gm_tmpl gm_tpln8ctpo9hror7;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tploa995k06npbj;
extern const gm_tmpl gm_tplogq0vb9du4ii;
extern const gm_tmpl gm_tplom6fga2t4kfv;
extern const gm_tmpl gm_tplorj2ck0ovl7t;
extern const gm_tmpl gm_tplovlajhkvntql;
extern const gm_tmpl gm_tplpehi2hdi7j7m;
extern const gm_tmpl gm_tplpf7ckn15kjth;
extern const gm_tmpl gm_tplpkq4i4j83tmk;
extern const gm_tmpl gm_tplpntqck8i8f5b;
extern const gm_tmpl gm_tplpoe8f3eln4m2;
extern const gm_tmpl gm_tplqm5shuhajt4a;
extern const gm_tmpl gm_tplqn6e8rqmlvpo;
extern const gm_tmpl gm_tplqn8qbim70q76;
extern const gm_tmpl gm_tplqp3jlt332oq5;
extern const gm_tmpl gm_tplr2tuff1mvdst;
extern const gm_tmpl gm_tplr5guiqmg1mn9;
extern const gm_tmpl gm_tplr9kq0pku58q3;
extern const gm_tmpl gm_tplrq1qj3euarkk;
extern const gm_tmpl gm_tpls4d1lh9jd2u8;
extern const gm_tmpl gm_tpls7kn7htisa6n;
extern const gm_tmpl gm_tplsh3mbm625h0g;
extern const gm_tmpl gm_tplsns868vihk0u;
extern const gm_tmpl gm_tplso4fjesdi372;
extern const gm_tmpl gm_tplspkvoi1o9of6;
extern const gm_tmpl gm_tpltktipp7m43tk;
extern const gm_tmpl gm_tplu65cpe6nhlbb;
extern const gm_tmpl gm_tplu7gkm8brh8s9;
extern const gm_tmpl gm_tplu9uohqqitldd;
extern const gm_tmpl gm_tplui184heavke2;
extern const gm_tmpl gm_tplut4ib9ov72oe;
extern const gm_tmpl gm_tplvbotf15ps5s5;
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tybqb4gb1d92;
extern const gm_type gm_tyiiln09e647;
extern const gm_type gm_tysgeom7733j;
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_r70pji5 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)1ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)1ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_ijtp9lo();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64itgp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_64itgp2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ag0n4rn *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_ag0n4rn *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ag0n4rn *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9e2ubbk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9e2ubbk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_aeib2bc *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_aeib2bc *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_aeib2bc *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9v60a6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9v60a6q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_a834m1a *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_a834m1a *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_a834m1a *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cdr8adu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cdr8adu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_qon8vgm *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_qon8vgm *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_qon8vgm *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ds16s19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ds16s19 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_q33vns6 *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_q33vns6 *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_q33vns6 *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eph0ild(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eph0ild F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_q4qso5a *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_q4qso5a *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_q4qso5a *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hiq3u0r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hiq3u0r F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_avo85vj *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_avo85vj *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_avo85vj *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nn7to1o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nn7to1o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_abcvqrv *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_abcvqrv *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_abcvqrv *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulerodq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ulerodq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_afan86j *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_afan86j *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_afan86j *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a07tfib F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6uqn3rvm1p8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplh6c513o0038j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a09tbhs F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_t22qvc1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a14ppi0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stm1tprvbfoip));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9kqhb5r0uube), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a1ch927 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srhrn3fa70ni4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6rsespo9a6nq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a1ec0ji F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh8j22p8lpudd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqn6e8rqmlvpo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a1j85ht F;
    gm_t2u4qamcbbgu0 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t2u4qamcbbgu0 t3 = GM_LD(gm_t2u4qamcbbgu0, ((uint8_t *)a0));
    gm_f_format_92fech4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a2pi3bt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnlsebkdpqrk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl00rv7vuig9ur), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a4nv9mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a4nv9mc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sumd85rpgjjrv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleonu21sc9fi7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a4r7k06 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shqvr1vn78f0b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_cp3hiv2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl91b7sjgnmine), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a4vi4f7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suntqnp3vfhar));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj9vh1qm5m031), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a5p8et1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbim95dtkmqmv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplut4ib9ov72oe), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a612q5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a612q5e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbbre965bpo5f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplie81jau63n6g), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a7bth9i F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6sdj2gjsdah5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpkq4i4j83tmk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aa62g71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aa62g71 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgq6hvrcda4sp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcef5l6cvp5lt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_abb68p1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_abb68p1 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjeoqheucmn3m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_thognv9odov03 t3 = GM_LD(gm_thognv9odov03, ((uint8_t *)a0));
    gm_f_format_p95juln((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpehi2hdi7j7m), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aculdom F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svgaurqm99100));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_peglcgm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj6imuu9dopvn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_adbd97i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp7619gun38fi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj57b11lvidge), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_adcs7to F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s87c4jti7ujk5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_1o6fs38((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0epjr4745p10), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_adtncrl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slpi6dabivg2e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpf7ckn15kjth), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_afagrs5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdcigk5ajbcfi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl606t7k5n88ss), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_afn1q7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_afn1q7g F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2bumqed39p41));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(16));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllktp07diubcq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aghnr00 F;
    gm_t5g5vgung6h6s gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shhak6l2rrn43));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t5g5vgung6h6s t3 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)a0));
    gm_f_format_817tni2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu9uohqqitldd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ahg4b8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ahg4b8c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spc8ncjo0q4pi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljm6mn1j3nrs6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ahgk48j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3m5ql5voef28));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple75bobdc6q0g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_ahnpa17 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk4tdtvin0g62));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_2bh0ost((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbtqgdimqhkr1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_aiomp5b F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf3uq7o37v8ks));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplba0kefu5gu2c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ajjbrpr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si5knpkeaqca7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(136));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploa995k06npbj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_akc2snl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_akc2snl F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0963m82avtn1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple4r1aumd134l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_akfobpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_akfobpm F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgcf9cdlaf08n));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple59d4hgjgp3n), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_amt7kei F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1h7h9updp99l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf28d52hmmd2n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ane7f49(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ane7f49 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5dln0neuav6j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl34lim186g0fm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_anrgoqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_anrgoqv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scommn379heqq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc0jhs849b438), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aome4un F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4cdme6b7eogn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(55));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr9kq0pku58q3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aoqs6rj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aoqs6rj F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smpdttgra9a1b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1jgrbdjt2sm3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aovijlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aovijlt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg4pgef1hupjq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu65cpe6nhlbb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_apg2igl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_apg2igl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5j95frjqt5af));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplogq0vb9du4ii), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_apgk46s F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s20mq4dk47bdd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl20t94jlvdndv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aq84o6u F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sltvces0c7l80));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplom6fga2t4kfv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aqb9tiu F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srv5rbhjb8ns5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_he6jnp0((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2u97uspakqd9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aqpcar1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aqpcar1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1h9rpfuhvh29));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcq107msa8ktd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_arnesr1 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6nbs6o2qkgvg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhp4k6h527j3k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_arud364(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_arud364 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snpp7pgdikfns));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqm5shuhajt4a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_as0b1t3 F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_supnh5d0f56vu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln3j86kdpqd7a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_askmr98 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sua99rsdk6d1r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplem02qig8laoi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_atch2pp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2c24uim36k06));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld4u3te7g9c5v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_atintr3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sevm6b5otfta5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpntqck8i8f5b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_avhd7c9 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_suvh4o70k4cg3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_h12an8k((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr5guiqmg1mn9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_avnkm9g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_avnkm9g F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_solguqm0afvc4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls7kn7htisa6n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q07lmui F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sisd1n433forg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2a8tmpvk11vt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q0doeth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_q0doeth F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssp208k4afn15));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_0dva8tv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9gmvk2gm4l5h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q190eul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q190eul F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1jim1okq5o2b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(191));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6ilgcn878l74), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q296tqt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc22qkqa9q462));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkpqhnfan9vel), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q2e4fnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q2e4fnf F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqmd0r48ameas));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl64ltc7suc5qg), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q2omjae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q2omjae F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj64h79nh4kvi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvbotf15ps5s5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q2uenf3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slbn3chj3p1e1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplellk9btkq1f2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q32bjcv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3k1sno8a2ipv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl97n87h3pcva1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q36d130(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q36d130 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sims8gu714a9a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu7gkm8brh8s9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q3eb2dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q3eb2dn F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sasnane8pan26));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfmqnc0q3nc41), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q3io8lg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sps1dthki4jpu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0o80ilivc5vg), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q451tst F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slkoo2ifkem47));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(61));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhsmbcv8t80v3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q62qvnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q62qvnl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn06hdjl3nknj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(86));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpoe8f3eln4m2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q6prg09 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc96am1bplmoh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(79));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_rjuhqv1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2r84u6vnc58k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q6qvtu0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q6qvtu0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm30gjvs8dlav));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl051t251mssfu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q8f0n70 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3o2km4i906va));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplui184heavke2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q8i7gsb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_summega2vonc9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllfhth9mee4q8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q8r21un F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdm98dp52j8ep));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls4d1lh9jd2u8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8uj6ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q8uj6ou F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8rv63m5o5imh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_temv198mna2jp t3 = GM_LD(gm_temv198mna2jp, ((uint8_t *)a0));
    gm_f_format_13o7g7i((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1u3j3qlmtjj2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qaapvnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qaapvnn F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sejbaeqb5aort));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_ttld71ltp94ka t3 = GM_LD(gm_ttld71ltp94ka, ((uint8_t *)a0));
    gm_f_format_ln6s15q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsns868vihk0u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_qc4s0m5 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stj45lnsahel3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplorj2ck0ovl7t), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qcp7nf9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn0il8abv9oq3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg4866gcgr837), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qds4t66 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shror0skne60i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkh4k0ikfsmp6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qe2mgnq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa92qppdjht4t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(99));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldc3nmdc0cit1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qe94i5r F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skdhvc36gfkb0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplovlajhkvntql), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qfgmn9b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qfgmn9b F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssld67umkdenq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplspkvoi1o9of6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qh5v7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_qh5v7ts F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2m3pa8n4g75s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln8ctpo9hror7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qhad3qp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8u62e7lr2m4s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl09kglfpefe8i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qilugr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qilugr5 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3udkpu8iv0h2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(74));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltktipp7m43tk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qjdh7n5 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sue9kqmk98bh9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2fb8qlpeqlet), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qjfu8g6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd1tvohjm34vg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpli9uflldtkfq3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qk20moi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1cbu3jc19u4t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbn5t5stpvru2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ql3u2no(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ql3u2no F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_surt6bemh510v));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgr8ep9alj0ph), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qm09p7m F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sks5ueb3h9e10));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1ige45obm306), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qner803(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qner803 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s129prqlnb2kt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldg1m204qb08c), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qofaffd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4mij084uljjf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcudcaj1iluqd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qpj4o1j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_qpj4o1j F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6dtn4tje3j7q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpleiompgmn2730), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qpmmpss F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svi913vrigpr0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(106));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplaoj5r4tauejk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qqtqagv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qqtqagv F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skucss3rkslf9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplae6k9mv0s1dm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_qr5c6un F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmsgkj4jeo7n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk5t5jhqqvnff), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qskma1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qskma1s F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3kfpoo7v2f2o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_6hrn7j8((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplld9eu0oenqb7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qu5usm7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95g98ft0cnt1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(154));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsh3mbm625h0g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qu7vf4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qu7vf4f F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suskhmu5llblc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7eehu7c1eqka), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qv493su F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s27tmg3h56aru));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrq1qj3euarkk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qvc703s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qvc703s F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5url3si4frr2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(68));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplho1kg74nlm64), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qvsrn69(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qvsrn69 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s626miklr3ggr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqp3jlt332oq5), t3);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_d1qo8mf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
    uint64_t t9 = r7;
    uint64_t t10 = gm_l1;
    uint8_t t11 = (t9 >= t10);
    if (t11) {
        gm_tftb1odc739t5 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        ((uint8_t *)&t12)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t12 + 1), GM_ZERO(gm_b0_1));
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    uint64_t t14 = gm_l1;
    gm_b16_8 t15 = gm_f_empty_slice_ijtp9lo();
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t13, t14, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64itgp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9e2ubbk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9v60a6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cdr8adu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ds16s19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eph0ild(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hiq3u0r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nn7to1o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulerodq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a4nv9mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a612q5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a834m1a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_a834m1a F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)56ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 56);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_9v60a6q, (void *)&F.h, cx});
    void * t12 = gm_l4;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
    const gm_tmpl *tp17 = (const gm_tmpl *)(t12);
    const void *vp17[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16))};
    const gm_type *vd17[] = {&gm_ty467d5cm1d5, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e};
    static const uint8_t vs17[] = {0, 1, 1, 1};
    gm_closure t18 = t11;
    gm_node *ccx19 = t18.cx;
    for (uint32_t k17 = 0;; k17++) {
        if ((tp17->text[k17].len != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, tp17->text[k17].ptr, tp17->text[k17].len))) {
            goto gm_lad;
        }
        if (k17 == tp17->n) break;
        uint32_t j17 = tp17->arg[k17];
        if (((!tp17->fmt[k17] && vs17[j17]) ? (GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, vp17[j17]), GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8))) : gm_pfk8csn7us23(t18.code, t18.env, ccx19, vd17[j17], vp17[j17], tp17->fmt[k17]))) {
            goto gm_lad;
        }
    }
    uint8_t t20 = F.l2;
    if (t20) {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t21);
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t22);
        goto gm_fdone;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
extern __attribute__((aligned(4))) void gm_f_format_aa62g71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_abb68p1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_abcvqrv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_abcvqrv F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)160ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 160);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_nn7to1o, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 112));
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 128));
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 144));
    const gm_tmpl *tp23 = (const gm_tmpl *)(t12);
    const void *vp23[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16)), (const void *)(((uint8_t *)&t17)), (const void *)(((uint8_t *)&t18)), (const void *)(((uint8_t *)&t19)), (const void *)(((uint8_t *)&t20)), (const void *)(((uint8_t *)&t21)), (const void *)(((uint8_t *)&t22))};
    const gm_type *vd23[] = {&gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e};
    static const uint8_t vs23[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    gm_closure t24 = t11;
    gm_node *ccx25 = t24.cx;
    for (uint32_t k23 = 0;; k23++) {
        if ((tp23->text[k23].len != 0 && gm_pf0aqhubr4up(t24.code, t24.env, ccx25, tp23->text[k23].ptr, tp23->text[k23].len))) {
            goto gm_lad;
        }
        if (k23 == tp23->n) break;
        uint32_t j23 = tp23->arg[k23];
        if (((!tp23->fmt[k23] && vs23[j23]) ? (GM_LD(uintptr_t, (const uint8_t *)vp23[j23] + 8) != 0 && gm_pf0aqhubr4up(t24.code, t24.env, ccx25, GM_LD(void *, vp23[j23]), GM_LD(uintptr_t, (const uint8_t *)vp23[j23] + 8))) : gm_pfk8csn7us23(t24.code, t24.env, ccx25, vd23[j23], vp23[j23], tp23->fmt[k23]))) {
            goto gm_lad;
        }
    }
    uint8_t t26 = F.l2;
    if (t26) {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t27);
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t28);
        goto gm_fdone;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
extern __attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aeib2bc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_aeib2bc F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 48);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_9e2ubbk, (void *)&F.h, cx});
    void * t12 = gm_l4;
    __int128 t13 = GM_LD(__int128, ((uint8_t *)&F.l3));
    __int128 t14 = GM_LD(__int128, (((uint8_t *)&F.l3) + 16));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
    const gm_tmpl *tp16 = (const gm_tmpl *)(t12);
    const void *vp16[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15))};
    const gm_type *vd16[] = {&gm_tyiiln09e647, &gm_tyiiln09e647, &gm_tyb2nno7n67r};
    static const uint8_t vs16[] = {0, 0, 0};
    gm_closure t17 = t11;
    gm_node *ccx18 = t17.cx;
    for (uint32_t k16 = 0;; k16++) {
        if ((tp16->text[k16].len != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, tp16->text[k16].ptr, tp16->text[k16].len))) {
            goto gm_lad;
        }
        if (k16 == tp16->n) break;
        uint32_t j16 = tp16->arg[k16];
        if (((!tp16->fmt[k16] && vs16[j16]) ? (GM_LD(uintptr_t, (const uint8_t *)vp16[j16] + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, vp16[j16]), GM_LD(uintptr_t, (const uint8_t *)vp16[j16] + 8))) : gm_pfk8csn7us23(t17.code, t17.env, ccx18, vd16[j16], vp16[j16], tp16->fmt[k16]))) {
            goto gm_lad;
        }
    }
    uint8_t t19 = F.l2;
    if (t19) {
        void * t20 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t20);
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t22);
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
extern __attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_afan86j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_afan86j F;
    gm_tjj2cncin95t1 gm_l0; /* args */
    gm_tjj2cncin95t1 gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    gm_tjj2cncin95t1 t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_ulerodq, (void *)&F.h, cx});
    void * t13 = gm_l4;
    gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
    const gm_tmpl *tp15 = (const gm_tmpl *)(t13);
    const void *vp15[] = {(const void *)(((uint8_t *)&t14))};
    const gm_type *vd15[] = {&gm_tysgeom7733j};
    static const uint8_t vs15[] = {0};
    gm_closure t16 = t12;
    gm_node *ccx17 = t16.cx;
    for (uint32_t k15 = 0;; k15++) {
        if ((tp15->text[k15].len != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, tp15->text[k15].ptr, tp15->text[k15].len))) {
            goto gm_lad;
        }
        if (k15 == tp15->n) break;
        uint32_t j15 = tp15->arg[k15];
        if (((!tp15->fmt[k15] && vs15[j15]) ? (GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, vp15[j15]), GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8))) : gm_pfk8csn7us23(t16.code, t16.env, ccx17, vd15[j15], vp15[j15], tp15->fmt[k15]))) {
            goto gm_lad;
        }
    }
    uint8_t t18 = F.l2;
    if (t18) {
        void * t19 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t19);
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t21);
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
extern __attribute__((aligned(4))) void gm_f_format_afn1q7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ag0n4rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_ag0n4rn F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)56ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 56);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_64itgp2, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
    const gm_tmpl *tp17 = (const gm_tmpl *)(t12);
    const void *vp17[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16))};
    const gm_type *vd17[] = {&gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty467d5cm1d5, &gm_ty0iqiubfm7e};
    static const uint8_t vs17[] = {1, 1, 0, 1};
    gm_closure t18 = t11;
    gm_node *ccx19 = t18.cx;
    for (uint32_t k17 = 0;; k17++) {
        if ((tp17->text[k17].len != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, tp17->text[k17].ptr, tp17->text[k17].len))) {
            goto gm_lad;
        }
        if (k17 == tp17->n) break;
        uint32_t j17 = tp17->arg[k17];
        if (((!tp17->fmt[k17] && vs17[j17]) ? (GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, vp17[j17]), GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8))) : gm_pfk8csn7us23(t18.code, t18.env, ccx19, vd17[j17], vp17[j17], tp17->fmt[k17]))) {
            goto gm_lad;
        }
    }
    uint8_t t20 = F.l2;
    if (t20) {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t21);
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t22);
        goto gm_fdone;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
extern __attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ahg4b8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
extern __attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_ajecpf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_akc2snl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_akfobpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ane7f49(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_anrgoqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_aoqs6rj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_aovijlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_apg2igl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_aqpcar1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_arud364(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) void gm_f_format_asjuqmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_avnkm9g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_avo85vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t6d4s85lju6dd a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_avo85vj F;
    gm_t6d4s85lju6dd gm_l0; /* args */
    gm_t6d4s85lju6dd gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)2ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    gm_t6d4s85lju6dd t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_hiq3u0r, (void *)&F.h, cx});
    void * t13 = gm_l4;
    gm_ts6oqppjh0ba3 t14 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l3));
    const gm_tmpl *tp15 = (const gm_tmpl *)(t13);
    const void *vp15[] = {(const void *)(((uint8_t *)&t14))};
    const gm_type *vd15[] = {&gm_tybqb4gb1d92};
    static const uint8_t vs15[] = {0};
    gm_closure t16 = t12;
    gm_node *ccx17 = t16.cx;
    for (uint32_t k15 = 0;; k15++) {
        if ((tp15->text[k15].len != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, tp15->text[k15].ptr, tp15->text[k15].len))) {
            goto gm_lad;
        }
        if (k15 == tp15->n) break;
        uint32_t j15 = tp15->arg[k15];
        if (((!tp15->fmt[k15] && vs15[j15]) ? (GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, vp15[j15]), GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8))) : gm_pfk8csn7us23(t16.code, t16.env, ccx17, vd15[j15], vp15[j15], tp15->fmt[k15]))) {
            goto gm_lad;
        }
    }
    uint8_t t18 = F.l2;
    if (t18) {
        void * t19 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t19);
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t21);
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
extern __attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q0doeth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) void gm_f_format_q190eul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q2e4fnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q2omjae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q33vns6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_q33vns6 F;
    gm_tkpb4kplftgaa gm_l0; /* args */
    gm_tkpb4kplftgaa gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&gm_l3), ((uint8_t *)a0), 24);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_ds16s19, (void *)&F.h, cx});
    void * t12 = gm_l4;
    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
    const gm_tmpl *tp16 = (const gm_tmpl *)(t12);
    const void *vp16[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15))};
    const gm_type *vd16[] = {&gm_tyb2nno7n67r, &gm_tyb2nno7n67r, &gm_tyb2nno7n67r};
    static const uint8_t vs16[] = {0, 0, 0};
    gm_closure t17 = t11;
    gm_node *ccx18 = t17.cx;
    for (uint32_t k16 = 0;; k16++) {
        if ((tp16->text[k16].len != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, tp16->text[k16].ptr, tp16->text[k16].len))) {
            goto gm_lad;
        }
        if (k16 == tp16->n) break;
        uint32_t j16 = tp16->arg[k16];
        if (((!tp16->fmt[k16] && vs16[j16]) ? (GM_LD(uintptr_t, (const uint8_t *)vp16[j16] + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, vp16[j16]), GM_LD(uintptr_t, (const uint8_t *)vp16[j16] + 8))) : gm_pfk8csn7us23(t17.code, t17.env, ccx18, vd16[j16], vp16[j16], tp16->fmt[k16]))) {
            goto gm_lad;
        }
    }
    uint8_t t19 = F.l2;
    if (t19) {
        void * t20 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t20);
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t22);
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
extern __attribute__((aligned(4))) void gm_f_format_q36d130(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q3eb2dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q4qso5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_q4qso5a F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)112ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 112);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_eph0ild, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
    const gm_tmpl *tp20 = (const gm_tmpl *)(t12);
    const void *vp20[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16)), (const void *)(((uint8_t *)&t17)), (const void *)(((uint8_t *)&t18)), (const void *)(((uint8_t *)&t19))};
    const gm_type *vd20[] = {&gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_tybihgc69jv3, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_tybihgc69jv3};
    static const uint8_t vs20[] = {1, 1, 1, 0, 1, 1, 0};
    gm_closure t21 = t11;
    gm_node *ccx22 = t21.cx;
    for (uint32_t k20 = 0;; k20++) {
        if ((tp20->text[k20].len != 0 && gm_pf0aqhubr4up(t21.code, t21.env, ccx22, tp20->text[k20].ptr, tp20->text[k20].len))) {
            goto gm_lad;
        }
        if (k20 == tp20->n) break;
        uint32_t j20 = tp20->arg[k20];
        if (((!tp20->fmt[k20] && vs20[j20]) ? (GM_LD(uintptr_t, (const uint8_t *)vp20[j20] + 8) != 0 && gm_pf0aqhubr4up(t21.code, t21.env, ccx22, GM_LD(void *, vp20[j20]), GM_LD(uintptr_t, (const uint8_t *)vp20[j20] + 8))) : gm_pfk8csn7us23(t21.code, t21.env, ccx22, vd20[j20], vp20[j20], tp20->fmt[k20]))) {
            goto gm_lad;
        }
    }
    uint8_t t23 = F.l2;
    if (t23) {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t24);
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t25);
        goto gm_fdone;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
extern __attribute__((aligned(4))) void gm_f_format_q62qvnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q6qvtu0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_q8uj6ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qaapvnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qfgmn9b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qh5v7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qilugr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ql3u2no(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qner803(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qon8vgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_qon8vgm F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)112ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 112);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_cdr8adu, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
    const gm_tmpl *tp20 = (const gm_tmpl *)(t12);
    const void *vp20[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16)), (const void *)(((uint8_t *)&t17)), (const void *)(((uint8_t *)&t18)), (const void *)(((uint8_t *)&t19))};
    const gm_type *vd20[] = {&gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_tybihgc69jv3, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e};
    static const uint8_t vs20[] = {1, 1, 1, 0, 1, 1, 1};
    gm_closure t21 = t11;
    gm_node *ccx22 = t21.cx;
    for (uint32_t k20 = 0;; k20++) {
        if ((tp20->text[k20].len != 0 && gm_pf0aqhubr4up(t21.code, t21.env, ccx22, tp20->text[k20].ptr, tp20->text[k20].len))) {
            goto gm_lad;
        }
        if (k20 == tp20->n) break;
        uint32_t j20 = tp20->arg[k20];
        if (((!tp20->fmt[k20] && vs20[j20]) ? (GM_LD(uintptr_t, (const uint8_t *)vp20[j20] + 8) != 0 && gm_pf0aqhubr4up(t21.code, t21.env, ccx22, GM_LD(void *, vp20[j20]), GM_LD(uintptr_t, (const uint8_t *)vp20[j20] + 8))) : gm_pfk8csn7us23(t21.code, t21.env, ccx22, vd20[j20], vp20[j20], tp20->fmt[k20]))) {
            goto gm_lad;
        }
    }
    uint8_t t23 = F.l2;
    if (t23) {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t24);
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t25);
        goto gm_fdone;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
extern __attribute__((aligned(4))) void gm_f_format_qpj4o1j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qqtqagv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_qskma1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_qu7vf4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qvc703s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_qvsrn69(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);

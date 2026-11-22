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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t a[65536]; } gm_t8qrv9nkfoifs;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t8qrv9nkfoifs) == 65536 && _Alignof(gm_t8qrv9nkfoifs) == 1 && offsetof(gm_t8qrv9nkfoifs, a) == 0, "layout of `[65536]u8`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { void * f0; gm_tm98ap4112hee f1; uint32_t f2; uint8_t pe[4]; } gm_tci6c4can0r43;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; uint64_t v3; uint64_t v4; uint64_t v5; uint64_t v6; uint64_t v7; uint64_t v8; gm_t9qkh0rsooc8d v9; } f1; } gm_tekvgkj2hms37;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; uint8_t f3; uint8_t pe[7]; } gm_t3irkpvggplij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_trqfvmjtt4sij f2; uint64_t f3; uint8_t f4; uint8_t pe[7]; } gm_t6cm8mrseh8uc;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tapvs478p7gjp;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tci6c4can0r43) == 24 && _Alignof(gm_tci6c4can0r43) == 8 && offsetof(gm_tci6c4can0r43, f0) == 0 && offsetof(gm_tci6c4can0r43, f1) == 8 && offsetof(gm_tci6c4can0r43, f2) == 16, "layout of `WorkerOut`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tekvgkj2hms37) == 16 && _Alignof(gm_tekvgkj2hms37) == 8 && offsetof(gm_tekvgkj2hms37, f0) == 0 && offsetof(gm_tekvgkj2hms37, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; uint8_t f2; uint8_t pe[7]; } gm_tn3i9u3h82tga;
typedef struct { gm_t3ujhu6qph7de f0; uint8_t f1; uint8_t pe[7]; } gm_tnnrsfd8ehhme;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint64_t f0; gm_tnpifv20irnph f1; } gm_tscv5kd4uabs1;
typedef struct { uint32_t f0; gm_tqnlpelep1582 f1; gm_b16_8 f2; } gm_tsqm4sdtus60a;
typedef struct { void * f0; gm_b16_8 f1; gm_tppo2hmut88dq f2; uint32_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; gm_trqfvmjtt4sij f8; gm_trqfvmjtt4sij f9; uint32_t f10; uint8_t pe[4]; } gm_ttgknnvmqfhok;
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t3irkpvggplij) == 80 && _Alignof(gm_t3irkpvggplij) == 8 && offsetof(gm_t3irkpvggplij, f0) == 0 && offsetof(gm_t3irkpvggplij, f1) == 24 && offsetof(gm_t3irkpvggplij, f2) == 48 && offsetof(gm_t3irkpvggplij, f3) == 72, "layout of `Seen`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t6cm8mrseh8uc) == 64 && _Alignof(gm_t6cm8mrseh8uc) == 8 && offsetof(gm_t6cm8mrseh8uc, f0) == 0 && offsetof(gm_t6cm8mrseh8uc, f1) == 4 && offsetof(gm_t6cm8mrseh8uc, f2) == 8 && offsetof(gm_t6cm8mrseh8uc, f3) == 48 && offsetof(gm_t6cm8mrseh8uc, f4) == 56, "layout of `Inbox`");
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_tapvs478p7gjp) == 56 && _Alignof(gm_tapvs478p7gjp) == 8 && offsetof(gm_tapvs478p7gjp, f0) == 0 && offsetof(gm_tapvs478p7gjp, f1) == 4 && offsetof(gm_tapvs478p7gjp, f2) == 8 && offsetof(gm_tapvs478p7gjp, f3) == 48, "layout of `Events`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
typedef struct { gm_t8vuock3jnv59 f0; gm_tjt4ka19str13 f1; } gm_thshlh2q7ekfv;
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; uint64_t f9; } gm_tmnkfo49014kk;
_Static_assert(sizeof(gm_tn3i9u3h82tga) == 48 && _Alignof(gm_tn3i9u3h82tga) == 8 && offsetof(gm_tn3i9u3h82tga, f0) == 0 && offsetof(gm_tn3i9u3h82tga, f1) == 16 && offsetof(gm_tn3i9u3h82tga, f2) == 40, "layout of `Kept`");
_Static_assert(sizeof(gm_tnnrsfd8ehhme) == 32 && _Alignof(gm_tnnrsfd8ehhme) == 8 && offsetof(gm_tnnrsfd8ehhme, f0) == 0 && offsetof(gm_tnnrsfd8ehhme, f1) == 24, "layout of `?Incoming`");
_Static_assert(sizeof(gm_tscv5kd4uabs1) == 24 && _Alignof(gm_tscv5kd4uabs1) == 8 && offsetof(gm_tscv5kd4uabs1, f0) == 0 && offsetof(gm_tscv5kd4uabs1, f1) == 8, "layout of `(i: usize, x: Unanswered)`");
_Static_assert(sizeof(gm_tsqm4sdtus60a) == 32 && _Alignof(gm_tsqm4sdtus60a) == 8 && offsetof(gm_tsqm4sdtus60a, f0) == 0 && offsetof(gm_tsqm4sdtus60a, f1) == 4 && offsetof(gm_tsqm4sdtus60a, f2) == 16, "layout of `(gen: u32, status: ?ExitStatus, err: []u8)`");
_Static_assert(sizeof(gm_ttgknnvmqfhok) == 136 && _Alignof(gm_ttgknnvmqfhok) == 8 && offsetof(gm_ttgknnvmqfhok, f0) == 0 && offsetof(gm_ttgknnvmqfhok, f1) == 8 && offsetof(gm_ttgknnvmqfhok, f2) == 24 && offsetof(gm_ttgknnvmqfhok, f3) == 40 && offsetof(gm_ttgknnvmqfhok, f4) == 44 && offsetof(gm_ttgknnvmqfhok, f5) == 45 && offsetof(gm_ttgknnvmqfhok, f6) == 46 && offsetof(gm_ttgknnvmqfhok, f7) == 47 && offsetof(gm_ttgknnvmqfhok, f8) == 48 && offsetof(gm_ttgknnvmqfhok, f9) == 88 && offsetof(gm_ttgknnvmqfhok, f10) == 128, "layout of `Supervisor`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tiilmfd16v3fc v1; gm_tsqm4sdtus60a v2; } f1; } gm_tumavt1402akl;
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_thshlh2q7ekfv) == 72 && _Alignof(gm_thshlh2q7ekfv) == 8 && offsetof(gm_thshlh2q7ekfv, f0) == 0 && offsetof(gm_thshlh2q7ekfv, f1) == 24, "layout of `Document`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_thshlh2q7ekfv v1; } f1; } gm_tit4ajsn3pv91;
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
_Static_assert(sizeof(gm_tmnkfo49014kk) == 304 && _Alignof(gm_tmnkfo49014kk) == 8 && offsetof(gm_tmnkfo49014kk, f0) == 0 && offsetof(gm_tmnkfo49014kk, f1) == 8 && offsetof(gm_tmnkfo49014kk, f2) == 64 && offsetof(gm_tmnkfo49014kk, f3) == 120 && offsetof(gm_tmnkfo49014kk, f4) == 176 && offsetof(gm_tmnkfo49014kk, f5) == 232 && offsetof(gm_tmnkfo49014kk, f6) == 288 && offsetof(gm_tmnkfo49014kk, f7) == 289 && offsetof(gm_tmnkfo49014kk, f8) == 290 && offsetof(gm_tmnkfo49014kk, f9) == 296, "layout of `Server`");
_Static_assert(sizeof(gm_tumavt1402akl) == 40 && _Alignof(gm_tumavt1402akl) == 8 && offsetof(gm_tumavt1402akl, f0) == 0 && offsetof(gm_tumavt1402akl, f1) == 8, "layout of `Event`");
_Static_assert(sizeof(gm_tit4ajsn3pv91) == 80 && _Alignof(gm_tit4ajsn3pv91) == 8 && offsetof(gm_tit4ajsn3pv91, f0) == 0 && offsetof(gm_tit4ajsn3pv91, f1) == 8, "layout of `ParseError!Document`");
struct gm_fr_as_array_nfvj8pk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_str_9rhg9v0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_clear_npuqd1h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_ptgum12 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_condvar_rp5e3qr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_crashed_vh3npqh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_60h290e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_l8j91qd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3kf9vt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e83i3qg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p8ki924 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vcgjvad {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exit_code_0nbqupf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_7904fsq {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_for_body_udhhmq1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_0cj9f6a {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_554rdam {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g88hhsh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h33rgps {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nsg49pd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_forward_client_h5f058d {
    gm_frame h;
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_from_client_liluprp {
    gm_frame h;
    gm_t3irkpvggplij l5; /* s */
    gm_b0_1 ret;
};
struct gm_fr_from_worker_quo4ehg {
    gm_frame h;
    gm_t3irkpvggplij l2; /* s */
    gm_b0_1 ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_inbox_h89r727 {
    gm_frame h;
    gm_t6cm8mrseh8uc ret;
};
struct gm_fr_is_initialize_4al2pub {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_open_a7p3eg6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keep_r1cc3er {
    gm_frame h;
    gm_t3irkpvggplij l3; /* s */
    gm_tn3i9u3h82tga l7; /* k */
    uint8_t ret;
};
struct gm_fr_last_report_2tphe2p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_50d680k {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kr0h52q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nb7om6u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_otmlne2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_message_6kqdnf1 {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_method_is_nfkh9v2 {
    gm_frame h;
    gm_t3irkpvggplij l2; /* s */
    uint8_t ret;
};
struct gm_fr_mutex_t7if00c {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_next_event_notphse {
    gm_frame h;
    gm_tumavt1402akl l1; /* e */
    gm_tumavt1402akl ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_path2_enc7vaj {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_post_u7g5gcu {
    gm_frame h;
    gm_tumavt1402akl l1; /* e */
    gm_b0_1 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_6sf2q21 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9b0vsek {
    gm_frame h;
    gm_tumavt1402akl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rvirr6r {
    gm_frame h;
    gm_tn3i9u3h82tga l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_worker_kl9k6j8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pending */
    gm_trqfvmjtt4sij l2; /* tail */
    gm_t8qrv9nkfoifs l3; /* buf */
    gm_trqfvmjtt4sij l8; /* rest */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8ovbb75 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_d8vk053 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q25h5ho {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_respond_raw_error_fc9dcul {
    gm_frame h;
    gm_thshlh2q7ekfv l2; /* doc */
    gm_b0_1 ret;
};
struct gm_fr_restart_62t9uv6 {
    gm_frame h;
    gm_tn3i9u3h82tga l3; /* k */
    uint8_t ret;
};
struct gm_fr_seen_b6ot3aa {
    gm_frame h;
    gm_thshlh2q7ekfv l1; /* doc */
    gm_t3irkpvggplij l2; /* r */
    gm_t3irkpvggplij ret;
};
struct gm_fr_signal_2drlsl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_start_lnda69t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_supervise_hvohndn {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* argv */
    gm_ttgknnvmqfhok l4; /* sup */
    gm_tmnkfo49014kk l5; /* srv */
    uint8_t ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_system_output_eg72lom {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_take_frame_nf67ep0 {
    gm_frame h;
    uint64_t l3; /* length */
    gm_trqfvmjtt4sij l7; /* rest */
    gm_th2lqbo77pvsc ret;
    gm_b0_1 w0;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_to_text_2cum4i7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_worker_bu6lrlk {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* msg */
    gm_b0_1 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unlock_9f1vcl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_wait_k1r8nlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_uqdv1oa {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_npuqd1h(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_ptgum12(void * a0);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_crashed_vh3npqh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tqnlpelep1582 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_deinit_60h290e(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3kf9vt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e83i3qg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p8ki924(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vcgjvad(void);
__attribute__((aligned(4))) uint8_t gm_f_eq_ignore_case_qqsqlkn(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_executable_lr5dqch(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_exit_code_0nbqupf(gm_tqnlpelep1582 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_udhhmq1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_0cj9f6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_554rdam(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g88hhsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_h33rgps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nsg49pd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_forward_client_h5f058d(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_from_client_liluprp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_from_worker_quo4ehg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
inline __attribute__((aligned(4))) void gm_f_inbox_h89r727(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_initialize_4al2pub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_open_a7p3eg6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_keep_r1cc3er(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_report_2tphe2p(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s0g5ifv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_50d680k(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kr0h52q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nb7om6u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_otmlne2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lock_u5lks01(void * a0);
__attribute__((aligned(4))) void gm_f_message_6kqdnf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_method_is_nfkh9v2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_7f4cigf(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_d0cb7fc(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_t835u2t(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_umo8ulm(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0, const void * a1);
__attribute__((aligned(4))) void gm_f_next_event_notphse(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tumavt1402akl gm_f_next_event_notphse_c(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_notify_ls6n69d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_parse_length_rbcv915(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_post_u7g5gcu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_post_u7g5gcu_c(void * a0, gm_tumavt1402akl a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6sf2q21(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9b0vsek(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rvirr6r(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_read_worker_kl9k6j8(void * a0);
__attribute__((aligned(4))) void gm_f_reader_4e2j89o(void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8ovbb75(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d8vk053(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q25h5ho(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_remove_j9nsdlp(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_remove_ljhb9o9(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_respond_error_5vpslhl(void *env, gm_node *cx, gm_node *inst, const void * a0, int64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_respond_raw_error_fc9dcul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_restart_62t9uv6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_run_rkae3vt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_seen_b6ot3aa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_server_6g9m6gg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_signal_2drlsl6(void * a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_0kkmakv(void * a0, void * a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_ic9k9n0(void * a0, void * a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_stdjo94(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_start_lnda69t(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_supervise_hvohndn(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void);
__attribute__((aligned(4))) void gm_f_take_frame_nf67ep0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_take_mg8jq2q(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_to_worker_bu6lrlk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5qef5td(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_message_bp6uv22(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqdv1oa(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_value_5haur8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hddfeetqn95b[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu50j4i95ke[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern void *const gm_hlm44rh1m4cu[];
extern uint8_t gm_k2o8n23kaot[40] __attribute__((aligned(8)));
extern uint8_t gm_k7ik3fa1b9i[40] __attribute__((aligned(8)));
extern uint8_t gm_k831oabi7o2[40] __attribute__((aligned(8)));
extern uint8_t gm_k91s9e2j9g7[40] __attribute__((aligned(8)));
extern uint8_t gm_kcdn8q7rsvc[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpqmkpiene[40] __attribute__((aligned(8)));
extern uint8_t gm_kebji8epnem[40] __attribute__((aligned(8)));
extern uint8_t gm_kgh95sj78n8[40] __attribute__((aligned(8)));
extern uint8_t gm_kihs3be4rv6[40] __attribute__((aligned(8)));
extern uint8_t gm_kj662j6koce[40] __attribute__((aligned(8)));
extern uint8_t gm_kjjevfvd6uv[40] __attribute__((aligned(8)));
extern uint8_t gm_kjn1j0gvnde[40] __attribute__((aligned(8)));
extern uint8_t gm_knvhkf6cslq[40] __attribute__((aligned(8)));
extern uint8_t gm_koalr8ndd0a[40] __attribute__((aligned(8)));
extern uint8_t gm_kq4dnvi7inu[40] __attribute__((aligned(8)));
extern uint8_t gm_kqiqqsf7tg0[40] __attribute__((aligned(8)));
extern uint8_t gm_kr4fbtke5rq[40] __attribute__((aligned(8)));
extern uint8_t gm_krhbibv34ui[40] __attribute__((aligned(8)));
extern uint8_t gm_kuspe9g7ejq[40] __attribute__((aligned(8)));
extern uint8_t gm_kvlrq0bgt7d[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0o9ngaj56qhu[12];
extern const uint8_t gm_s2431f6f423u2[21];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s352761194jbs[9];
extern const uint8_t gm_s37v6u2dqp80f[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s4j3ecogbjkvn[13];
extern const uint8_t gm_s779r09d5sq6o[33];
extern const uint8_t gm_s7bj0jma5l2mg[39];
extern const uint8_t gm_s80l47nhae4el[23];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_sc5bk1hdqs61j[22];
extern const uint8_t gm_scitmdqfeod1m[6];
extern const uint8_t gm_sdjba9ailb8m1[4];
extern const uint8_t gm_se01en57ticrr[33];
extern const uint8_t gm_sf4qi8ut6u3p1[5];
extern const uint8_t gm_sfkmkcagsnqnb[7];
extern const uint8_t gm_sfqr2pc72qt8e[23];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sgde423clem3e[48];
extern const uint8_t gm_sktnfpvsdatf3[22];
extern const uint8_t gm_sl4gq9e3k2i6t[19];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_smga32pj96fal[11];
extern const uint8_t gm_sn5qnjkjnmu95[23];
extern const uint8_t gm_snl36eh5psull[7];
extern const uint8_t gm_so1ub93q801tl[65];
extern const uint8_t gm_spqmq1ln67me5[79];
extern const uint8_t gm_sq3gi0chb3cs2[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4hj0beu8p6ao[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss3tptge0i2av[36];
extern const uint8_t gm_ssfcudki1m4ds[170];
extern const uint8_t gm_st3o1r587rnip[18];
extern const uint8_t gm_stum5f38vfqa2[15];
extern const uint8_t gm_su4gcaqk5i507[44];
extern const uint8_t gm_sujli4dml3ipq[8];
extern const uint8_t gm_sunu1gosuevl4[15];
extern const gm_tmpl gm_tpl1u8koiam5nmo;
extern const gm_tmpl gm_tpl4cbbis3gd1k3;
extern const gm_tmpl gm_tpl65nnuqtjd24o;
extern const gm_tmpl gm_tpl6q4b2p6tude6;
extern const gm_tmpl gm_tpln2i759e19cgt;
extern const gm_tmpl gm_tplomf18g3j9if3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_array_nfvj8pk F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* xs */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)4ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&gm_l2 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l2 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l2 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l2), t8);
            ((uint8_t *)&gm_l2)[16] = 1;
            goto gm_lb1;
        }
gm_lb2:;
        {
            memset(&gm_l2, 0, sizeof gm_l2);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 57, 41);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l2), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_9rhg9v0 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)3ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&gm_l2 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l2 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l2 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l2), t8);
            ((uint8_t *)&gm_l2)[16] = 1;
            goto gm_lb1;
        }
gm_lb2:;
        {
            memset(&gm_l2, 0, sizeof gm_l2);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 52, 38);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l2), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_npuqd1h(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_npuqd1h F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_ptgum12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ptgum12 F;
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_condvar_rp5e3qr F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3kf9vt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3kf9vt F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e83i3qg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e83i3qg F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p8ki924(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p8ki924 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vcgjvad(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vcgjvad F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_exit_code_0nbqupf(gm_tqnlpelep1582 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_code_0nbqupf F;
    gm_tqnlpelep1582 gm_l0; /* status */
    gm_tsoiri44d0i6q gm_l1; /* st */
    uint32_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l0))[8] != 0);
    if (t4) {
        gm_tsoiri44d0i6q t5 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&gm_l0));
        gm_l1 = t5;
    } else {
        F.ret = ((uint8_t)1ULL);
        goto gm_fret;
    }
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l2 = t8;
            uint32_t t9 = gm_l2;
            uint8_t t10 = (uint8_t)t9;
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)1ULL);
        goto gm_fret;
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_7904fsq F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* key */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_t8vuock3jnv59 gm_l2;
    gm_t3ph3kbiqt1ah gm_l3;
    {
        gm_b16_8 t2 = gm_l1;
        gm_t8vuock3jnv59 t3 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a0));
        gm_f_get_fjdb5ou((void *)&gm_l3, &t3, t2);
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[24] != 0);
    if (t4) {
        memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l3), 24);
    } else {
        GM_ST(uint8_t, (uint8_t *)&gm_l2 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l2 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l2 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l2 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l2 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&gm_l2 + 0), ((uint8_t)0ULL));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l2), 24);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_0cj9f6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0cj9f6a F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssfcudki1m4ds));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(169));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl65nnuqtjd24o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_554rdam(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_554rdam F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfqr2pc72qt8e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln2i759e19cgt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g88hhsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_g88hhsh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_se01en57ticrr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4cbbis3gd1k3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h33rgps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_h33rgps F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc5bk1hdqs61j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplomf18g3j9if3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nsg49pd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nsg49pd F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgde423clem3e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1u8koiam5nmo), t3);
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
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t3 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_inbox_h89r727(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_inbox_h89r727 F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
    gm_t88rcucg1b7uk t3 = GM_CONV(gm_t88rcucg1b7uk, gm_f_condvar_rp5e3qr());
    gm_trqfvmjtt4sij t4;
    gm_f_list_50d680k((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 60, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 4), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 8), t4);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 48), ((uint64_t)0ULL));
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6cm8mrseh8uc *)gm_out) + 56), ((uint8_t)0));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_initialize_4al2pub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_initialize_4al2pub F;
    gm_b16_8 gm_l0; /* body */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smga32pj96fal));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    uint8_t t4 = gm_f_method_is_nfkh9v2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_open_a7p3eg6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_open_a7p3eg6 F;
    gm_b16_8 gm_l0; /* body */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2431f6f423u2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    uint8_t t4 = gm_f_method_is_nfkh9v2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_50d680k(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_50d680k F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p8ki924();
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
inline __attribute__((aligned(4))) void gm_f_list_kr0h52q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kr0h52q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d3kf9vt();
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
inline __attribute__((aligned(4))) void gm_f_list_nb7om6u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nb7om6u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_e83i3qg();
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
inline __attribute__((aligned(4))) void gm_f_list_otmlne2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_otmlne2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vcgjvad();
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mutex_t7if00c F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_obj_aoa7fl7 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_792g2go_2((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path2_enc7vaj F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* a */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t2 = gm_l1;
    gm_t8vuock3jnv59 t3;
    gm_f_field_7904fsq((void *)&t3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t2);
    gm_b16_8 t4 = gm_l2;
    gm_f_field_7904fsq((void *)&(*(gm_t8vuock3jnv59 *)gm_out), &t3, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_post_u7g5gcu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_post_u7g5gcu F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_f_lock_u5lks01(t3);
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_tftb1odc739t5 t6 = gm_f_push_9b0vsek(t5, &(*(gm_tumavt1402akl *)((uint8_t *)&F.l1)));
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src4hj0beu8p6ao, 29, 5);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 4);
    gm_f_signal_2drlsl6(t9);
    void * t10 = gm_l0;
    void * t11 = (void *)((uint8_t *)t10);
    gm_f_unlock_9f1vcl6(t11);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6sf2q21(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_6sf2q21 F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_vcgjvad();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8ovbb75(t8, ((uint64_t)1ULL), t10);
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
    gm_tnpifv20irnph t18 = gm_l1;
    GM_ST(gm_tnpifv20irnph, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9b0vsek(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9b0vsek F;
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
        gm_b16_8 t9 = gm_f_empty_slice_d3kf9vt();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q25h5ho(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rvirr6r(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rvirr6r F;
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
        gm_b16_8 t9 = gm_f_empty_slice_e83i3qg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_d8vk053(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8ovbb75(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8ovbb75 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d8vk053(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_d8vk053 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q25h5ho(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q25h5ho F;
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
inline __attribute__((aligned(4))) void gm_f_signal_2drlsl6(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_signal_2drlsl6 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    uint32_t t5 = __atomic_fetch_add(((uint32_t *)t3), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    void * t6 = gm_l0;
    void * t7 = (void *)((uint8_t *)t6);
    int64_t t8 = gm_futex_wake(t7, ((uint32_t)1ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_output_eg72lom F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_ls49dp6 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 8), t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_text_2cum4i7 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
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
        gm_b0_1 t4 = gm_f_write_value_5haur8t((void *)NULL, cx, NULL, t3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unlock_9f1vcl6 F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    uint32_t t5 = __atomic_exchange_n(((uint32_t *)t3), ((uint32_t)0ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    uint8_t t6 = ((t5 == ((uint32_t)2ULL)));
    if (t6) {
        void * t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)t7);
        int64_t t9 = gm_futex_wake(t8, ((uint32_t)1ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_pkuqal3 F;
    void * gm_l0; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t8vuock3jnv59 *)gm_out) + 8), t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wait_k1r8nlm F;
    void * gm_l0; /* c */
    void * gm_l1; /* m */
    uint32_t gm_l2; /* seen */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    uint32_t t5 = __atomic_load_n(((uint32_t *)t3), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    gm_l2 = t5;
    void * t6 = gm_l1;
    gm_f_unlock_9f1vcl6(t6);
    void * t7 = gm_l0;
    void * t8 = (void *)((uint8_t *)t7);
    uint32_t t9 = gm_l2;
    int64_t t10 = gm_futex_wait(t8, t9, ((int64_t)-1LL));
    void * t11 = gm_l1;
    gm_f_lock_u5lks01(t11);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqdv1oa(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uqdv1oa F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s80l47nhae4el));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(22));
    gm_tftb1odc739t5 t5 = gm_f_write_5qef5td(t2, t3, ((void *)&gm_tpl6q4b2p6tude6), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_crashed_vh3npqh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tqnlpelep1582 a1, gm_b16_8 a2) {
    struct gm_fr_crashed_vh3npqh F;
    void * gm_l0; /* sup */
    gm_tqnlpelep1582 gm_l1; /* status */
    gm_b16_8 gm_l2; /* err */
    gm_b16_8 gm_l3; /* what */
    gm_b16_8 gm_l4; /* how */
    gm_tsoiri44d0i6q gm_l5; /* st */
    uint32_t gm_l6; /* c */
    int32_t gm_l7; /* n */
    gm_b16_8 gm_l8; /* summary */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tnpifv20irnph gm_l11; /* u */
    GM_STACK_CHECK(&F, 992u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    void * t12 = gm_l0;
    void * t13 = gm_l0;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 128));
    uint32_t r15; bool o16 = __builtin_add_overflow(t14, ((uint32_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 409, 5);
    uint32_t t17 = r15;
    GM_ST(uint32_t, (((uint8_t *)t12) + 128), t17);
    gm_b16_8 t18 = gm_l2;
    gm_b16_8 t19 = gm_f_last_report_2tphe2p(t18);
    gm_l3 = t19;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_scitmdqfeod1m));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
    gm_l4 = t20;
    {
        {
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l1))[8] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_tsoiri44d0i6q t22 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&gm_l1));
            gm_l5 = t22;
            {
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                    gm_l6 = t25;
                    uint32_t t26 = gm_l6;
                    gm_t88rcucg1b7uk t27;
                    GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
                    gm_f_format_h33rgps((void *)&gm_l13, (void *)NULL, cx, NULL, t27);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src4hj0beu8p6ao, 414, 27);
                    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                    gm_l12 = t30;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
                    if (!t32) {
                        goto gm_lb5;
                    }
                    int32_t t33 = GM_LD(int32_t, (((uint8_t *)&gm_l5) + 4));
                    gm_l7 = t33;
                    int32_t t34 = gm_l7;
                    gm_t19enj90vg8bd t35;
                    GM_ST(int32_t, ((uint8_t *)&t35 + 0), t34);
                    gm_f_format_554rdam((void *)&gm_l14, (void *)NULL, cx, NULL, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src4hj0beu8p6ao, 415, 29);
                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                    gm_l12 = t38;
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 413, 15);
            }
gm_lb3:;
            gm_b16_8 t39 = gm_l12;
            gm_l4 = t39;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t41 = (t40 > ((uint64_t)0ULL));
    if (t41) {
        gm_b16_8 t42 = gm_l3;
        gm_l8 = t42;
    } else {
        gm_b16_8 t43 = gm_l4;
        gm_l8 = t43;
    }
    {
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sl4gq9e3k2i6t));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(18));
        gm_b16_8 t45 = gm_l8;
        gm_tnpifv20irnph t46;
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
        gm_f_format_0cj9f6a((void *)&gm_l15, (void *)NULL, cx, NULL, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src4hj0beu8p6ao, 419, 45);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_t8vuock3jnv59 t50;
        gm_f_message_6kqdnf1((void *)&t50, (void *)NULL, cx, NULL, ((int64_t)1LL), t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t51 = gm_f_notify_ls6n69d((void *)NULL, cx, NULL, t44, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_st3o1r587rnip));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(17));
        gm_b16_8 t53 = gm_l4;
        gm_b16_8 t54 = gm_l2;
        gm_t6j6smj7qoctu t55;
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t53);
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 16), t54);
        gm_f_format_g88hhsh((void *)&gm_l16, (void *)NULL, cx, NULL, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src4hj0beu8p6ao, 420, 44);
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_t8vuock3jnv59 t59;
        gm_f_message_6kqdnf1((void *)&t59, (void *)NULL, cx, NULL, ((int64_t)1LL), t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t60 = gm_f_notify_ls6n69d((void *)NULL, cx, NULL, t52, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t61 = gm_l0;
    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 88));
    gm_l9 = t62;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t63 = gm_l10;
            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t65 = (t63 < t64);
            if (!t65) {
                goto gm_lb6;
            }
            {
                void * t66 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t68 = gm_l10;
                {
                    {
                        gm_tnpifv20irnph t69 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t66) + (uintptr_t)t68 * 16));
                        gm_l11 = t69;
                        {
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                            gm_b16_8 t72 = gm_l8;
                            gm_tnpifv20irnph t73;
                            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                            gm_f_format_nsg49pd((void *)&gm_l17, (void *)NULL, cx, NULL, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4hj0beu8p6ao, 422, 33);
                            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                            gm_b0_1 t77 = gm_f_respond_raw_error_fc9dcul((void *)NULL, cx, NULL, t71, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_b0_1 t80 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t79, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2o8n23kaot)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 421, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t81 = gm_l10;
            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
            uint64_t t84 = r82;
            gm_l10 = t84;
        }
    }
gm_lb6:;
    void * t85 = gm_l0;
    void * t86 = (void *)(((uint8_t *)t85) + 88);
    gm_f_clear_npuqd1h(t86);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_deinit_60h290e(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_deinit_60h290e F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* m */
    gm_b16_8 gm_l2; /* u */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = (uint8_t)((((uint8_t *)t6))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t6));
            gm_l1 = t8;
            gm_b16_8 t9 = gm_l1;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_b0_1 t11 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kvlrq0bgt7d)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t12 = gm_l0;
            uint8_t t13 = (uint8_t)(((((uint8_t *)t12) + 48))[16] != 0);
            if (!t13) {
                goto gm_lb4;
            }
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 48));
            gm_l2 = t14;
            gm_b16_8 t15 = gm_l2;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_b0_1 t17 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kuspe9g7ejq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_exit_code_0nbqupf(gm_tqnlpelep1582 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_udhhmq1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_udhhmq1 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_t6j6smj7qoctu gm_l2; /* split */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ttu09lij36f8c gm_l3;
    uint64_t gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
            gm_f_split_once_mqcu1ce((void *)&gm_l3, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[32] != 0);
            if (t7) {
                memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l3), 32);
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t9 = gm_f_trim_fdrfe6i(t8);
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_stum5f38vfqa2));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(14));
            uint8_t t11 = gm_f_eq_ignore_case_qqsqlkn(t9, t10);
            if (t11) {
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
                gm_b16_8 t13 = gm_f_trim_fdrfe6i(t12);
                gm_tf5srh4mp4b3i t14 = gm_f_parse_length_rbcv915(t13);
                gm_l5 = t14;
                uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                if (t15) {
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                    gm_l4 = t16;
                } else {
                    gm_l4 = ((uint64_t)0ULL);
                }
                uint64_t t17 = gm_l4;
                GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_take_frame_nf67ep0 *)((&F.h)->up))->l3), t17);
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 108, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_forward_client_h5f058d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_forward_client_h5f058d F;
    void * gm_l0; /* p */
    gm_t3ujhu6qph7de gm_l1; /* m */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnnrsfd8ehhme gm_l2;
    {
        gm_t3mf58gv9kf3n t6 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t7 = t6;
        gm_node wn8 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t7, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb2;
                }
                {
                    {
                        void * t11 = gm_l0;
                        void * t12 = GM_LD(void *, ((uint8_t *)t11));
                        gm_f_take_mg8jq2q((void *)&gm_l2, t12);
                    }
                    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l2))[24] != 0);
                    if (t13) {
                        memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 24);
                    } else {
                        goto gm_lb2;
                    }
                    {
                        void * t14 = gm_l0;
                        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 8));
                        gm_t3ujhu6qph7de t16 = GM_LD(gm_t3ujhu6qph7de, ((uint8_t *)&gm_l1));
                        gm_tumavt1402akl t17;
                        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t17 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t17 + 8), t16);
                        gm_f_post_u7g5gcu(t15, &t17);
                    }
                }
gm_lb4:;
            }
        }
gm_lb2:;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
        gm_tumavt1402akl t20;
        memset(&t20, 0, sizeof t20);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
        gm_f_post_u7g5gcu(t19, &t20);
    }
gm_lb1:;
we9:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_from_client_liluprp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_from_client_liluprp F;
    void * gm_l0; /* sup */
    gm_t3ujhu6qph7de gm_l1; /* m */
    gm_b16_8 gm_l2; /* e */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* body */
    gm_b16_8 gm_l6; /* method */
    gm_b16_8 gm_l7; /* id */
    uint8_t gm_l8; /* kept */
    gm_b16_8 gm_l9; /* id */
    GM_STACK_CHECK(&F, 848u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l10;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    uint8_t gm_l13;
    {
        {
            {
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
                if (!t18) {
                    goto gm_lb3;
                }
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
                gm_l2 = t19;
                gm_b16_8 t20 = gm_l2;
                gm_b0_1 t21 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)gm_cro0h2panct)), ((int64_t)-32700LL), t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb3:;
            {
                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                if (!t23) {
                    goto gm_lb4;
                }
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
                gm_l3 = t24;
                gm_b16_8 t25 = gm_l3;
                gm_l4 = t25;
                goto gm_lb2;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 316, 13);
        }
gm_lb2:;
        {
            gm_b16_8 t26 = gm_l4;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_t3irkpvggplij t28;
            gm_f_seen_b6ot3aa((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l5 = t28;
        }
        {
            gm_th2lqbo77pvsc t29 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&F.l5));
            gm_l10 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t29) + 0));
        }
        uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
        if (t30) {
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l6 = t31;
        } else {
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(0));
            gm_l6 = t32;
        }
        gm_b16_8 t33 = gm_l6;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s37v6u2dqp80f));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(4));
        uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
        if (t35) {
            void * t36 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t36) + 47), ((uint8_t)1));
        }
        {
            {
                uint8_t t37 = (uint8_t)(((((uint8_t *)&F.l5) + 24))[16] != 0);
                if (!t37) {
                    goto gm_lb6;
                }
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                gm_l7 = t38;
                {
                    {
                        uint8_t t39 = (uint8_t)((((uint8_t *)&F.l5))[16] != 0);
                        if (t39) {
                            goto gm_lb8;
                        }
                        gm_l11 = ((uint8_t)0);
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        gm_l11 = ((uint8_t)1);
                        goto gm_lb7;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 329, 12);
                }
gm_lb7:;
                uint8_t t40 = gm_l11;
                if (t40) {
                    void * t41 = gm_l0;
                    void * t42 = (void *)(((uint8_t *)t41) + 88);
                    gm_b16_8 t43 = gm_l7;
                    gm_tnpifv20irnph t44;
                    GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
                    gm_tftb1odc739t5 t45 = gm_f_push_6sf2q21(t42, t44);
                    gm_l12 = t45;
                    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src4hj0beu8p6ao, 329, 35);
                } else {
                    gm_b16_8 t47 = gm_l7;
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                    gm_b0_1 t49 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t48, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kr4fbtke5rq)));
                    if (gm_unw.kind) {
                        goto gm_ld16_1;
                    }
                }
                goto gm_lb5;
            }
gm_lb6:;
        }
gm_lb5:;
        {
            void * t50 = gm_l0;
            gm_b16_8 t51 = gm_l6;
            gm_b16_8 t52 = gm_l4;
            uint8_t t53 = gm_f_keep_r1cc3er((void *)NULL, cx, NULL, t50, t51, t52, &(*(gm_t3irkpvggplij *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_ld16_1;
            }
            gm_l8 = t53;
        }
        void * t54 = gm_l0;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t54) + 44));
        uint8_t t56 = (uint8_t)!t55;
        if (t56) {
            void * t57 = gm_l0;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t57) + 47));
            if (t58) {
                F.ret = GM_ZERO(gm_b0_1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld16_2;
            }
            void * t59 = gm_l0;
            uint8_t t60 = gm_f_restart_62t9uv6((void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_ld16_2;
            }
            uint8_t t61 = (uint8_t)!t60;
            if (t61) {
                {
                    {
                        uint8_t t62 = (uint8_t)((((uint8_t *)&F.l5))[16] != 0);
                        if (t62) {
                            goto gm_lb11;
                        }
                        gm_l13 = ((uint8_t)0);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        gm_l13 = ((uint8_t)1);
                        goto gm_lb10;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 337, 16);
                }
gm_lb10:;
                uint8_t t63 = gm_l13;
                if (t63) {
                    {
                        {
                            uint8_t t64 = (uint8_t)(((((uint8_t *)&F.l5) + 24))[16] != 0);
                            if (!t64) {
                                goto gm_lb14;
                            }
                            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                            gm_l9 = t65;
                            gm_b16_8 t66 = gm_l9;
                            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                            gm_b16_8 t68;
                            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_su4gcaqk5i507));
                            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(43));
                            gm_b0_1 t69 = gm_f_respond_raw_error_fc9dcul((void *)NULL, cx, NULL, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_ld16_2;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                    }
gm_lb13:;
                }
                F.ret = GM_ZERO(gm_b0_1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld16_2;
            }
            uint8_t t70 = gm_l8;
            uint8_t t71 = (uint8_t)!t70;
            if (t71) {
                void * t72 = gm_l0;
                gm_b16_8 t73 = gm_l4;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_b0_1 t75 = gm_f_to_worker_bu6lrlk((void *)NULL, cx, NULL, t72, t74);
                if (gm_unw.kind) {
                    goto gm_ld16_2;
                }
            }
        } else {
            void * t76 = gm_l0;
            gm_b16_8 t77 = gm_l4;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_b0_1 t79 = gm_f_to_worker_bu6lrlk((void *)NULL, cx, NULL, t76, t78);
            if (gm_unw.kind) {
                goto gm_ld16_2;
            }
        }
        void * t80 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t80) + 45), ((uint8_t)0));
    }
    {
        uint8_t t81 = gm_l8;
        uint8_t t82 = (uint8_t)!t81;
        if (t82) {
            gm_b16_8 t83 = gm_l4;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_b0_1 t85 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t84, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k831oabi7o2)));
        }
    }
    {
        void * t86 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t87 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t86);
    }
    goto gm_ls88;
gm_ld16_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        uint8_t t89 = gm_l8;
        uint8_t t90 = (uint8_t)!t89;
        if (t90) {
            gm_b16_8 t91 = gm_l4;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            gm_b0_1 t93 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t92, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k831oabi7o2)));
        }
        gm_unw = gm_saved;
    }
gm_ld16_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t94 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t95 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t94);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls88:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_from_worker_quo4ehg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_from_worker_quo4ehg F;
    void * gm_l0; /* sup */
    gm_b16_8 gm_l1; /* body */
    gm_b16_8 gm_l3; /* id */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_tnpifv20irnph gm_l7; /* u */
    GM_STACK_CHECK(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    gm_tscv5kd4uabs1 gm_l9;
    {
        gm_b16_8 t15 = gm_l1;
        gm_t3irkpvggplij t16;
        gm_f_seen_b6ot3aa((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t16;
        {
            {
                uint8_t t19 = (uint8_t)(((((uint8_t *)&F.l2) + 24))[16] != 0);
                if (!t19) {
                    goto gm_lb3;
                }
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
                gm_l3 = t20;
                {
                    {
                        {
                            uint8_t t22 = (uint8_t)((((uint8_t *)&F.l2))[16] != 0);
                            if (t22) {
                                goto gm_lb6;
                            }
                            gm_l8 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l8 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 390, 12);
                    }
gm_lb5:;
                    uint8_t t23 = gm_l8;
                    if (t23) {
                        void * t24 = gm_l0;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t24) + 46));
                        if (t25) {
                            void * t26 = gm_l0;
                            GM_ST(uint8_t, (((uint8_t *)t26) + 46), ((uint8_t)0));
                            F.ret = GM_ZERO(gm_b0_1);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                            goto gm_ld21_1;
                        }
                        void * t27 = gm_l0;
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 88));
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_l4 = t29;
                        gm_l5 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t30 = gm_l5;
                                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint8_t t32 = (t30 < t31);
                                if (!t32) {
                                    goto gm_lb8;
                                }
                                {
                                    uint64_t t33 = gm_l5;
                                    void * t34 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                    uint64_t t36 = gm_l5;
                                    gm_tnpifv20irnph t37 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t34) + (uintptr_t)t36 * 16));
                                    GM_ST(uint64_t, ((uint8_t *)&gm_l9 + 0), t33);
                                    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&gm_l9 + 8), t37);
                                    {
                                        {
                                            uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                                            gm_l6 = t38;
                                            gm_tnpifv20irnph t39 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l9) + 8));
                                            gm_l7 = t39;
                                            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                            gm_b16_8 t41 = gm_l3;
                                            uint8_t t42 = (gm_eqbihgc69jv3(((uint8_t *)&t40), ((uint8_t *)&t41)));
                                            if (t42) {
                                                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                                                gm_b0_1 t45 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t44, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqiqqsf7tg0)));
                                                if (gm_unw.kind) {
                                                    goto gm_ld21_1;
                                                }
                                                void * t46 = gm_l0;
                                                void * t47 = (void *)(((uint8_t *)t46) + 88);
                                                uint64_t t48 = gm_l6;
                                                gm_tnpifv20irnph t49 = gm_f_remove_ljhb9o9(t47, t48);
                                                goto gm_lb8;
                                            }
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 395, 13);
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                uint64_t t50 = gm_l5;
                                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                                uint64_t t53 = r51;
                                gm_l5 = t53;
                            }
                        }
gm_lb8:;
                    }
                }
                {
                    gm_b16_8 t54 = gm_l3;
                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                    gm_b0_1 t56 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t55, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kj662j6koce)));
                }
                goto gm_ls57;
gm_ld21_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    gm_b16_8 t58 = gm_l3;
                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                    gm_b0_1 t60 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t59, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kj662j6koce)));
                    gm_unw = gm_saved;
                }
                goto gm_ll18;
gm_ls57:;
gm_lb4:;
                goto gm_lb2;
            }
            goto gm_ls61;
gm_ll18:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741826) { gm_unw.kind = GM_NONE; goto gm_lb3; }
            goto gm_ll17;
gm_ls61:;
gm_lb3:;
        }
        goto gm_ls62;
gm_ll17:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741825) { gm_unw.kind = GM_NONE; goto gm_lb2; }
        goto gm_ld14_1;
gm_ls62:;
gm_lb2:;
        gm_b16_8 t63 = gm_l1;
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_b0_1 t65 = gm_f_write_message_bp6uv22((void *)NULL, cx, NULL, t64);
        if (gm_unw.kind) {
            goto gm_ld14_1;
        }
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t67 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t66);
    }
    goto gm_ls68;
gm_ld14_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t69 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t70 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t69);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls68:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_initialize_4al2pub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_open_a7p3eg6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_keep_r1cc3er(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_keep_r1cc3er F;
    void * gm_l0; /* sup */
    gm_b16_8 gm_l1; /* method */
    gm_b16_8 gm_l2; /* body */
    gm_b16_8 gm_l4; /* uri */
    uint8_t gm_l5; /* whole */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l8; /* ku */
    uint8_t gm_l9; /* same */
    gm_b16_8 gm_l10; /* ku */
    GM_STACK_CHECK(&F, 1091u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_tftb1odc739t5 gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tftb1odc739t5 gm_l16;
    {
        {
            {
                {
                    gm_b16_8 t24 = gm_l1;
                    gm_b16_8 t25;
                    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_smga32pj96fal));
                    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(10));
                    uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                    if (!t26) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_b16_8 t27 = gm_l1;
                    gm_b16_8 t28;
                    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s0o9ngaj56qhu));
                    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(11));
                    uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_b16_8 t30 = gm_l1;
                    gm_b16_8 t31;
                    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s779r09d5sq6o));
                    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(32));
                    uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
                    if (!t32) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    gm_b16_8 t33 = gm_l1;
                    gm_b16_8 t34;
                    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_ss3tptge0i2av));
                    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(35));
                    uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                    if (!t35) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            {
                void * t36 = gm_l0;
                void * t37 = (void *)(((uint8_t *)t36) + 48);
                gm_b16_8 t38 = gm_l2;
                gm_th2lqbo77pvsc t39;
                memset(&t39, 0, sizeof t39);
                gm_tn3i9u3h82tga t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 41, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 42, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 44, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t38);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t40 + 16), t39);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 40), ((uint8_t)0));
                gm_tftb1odc739t5 t41 = gm_f_push_rvirr6r(t37, &t40);
                gm_l11 = t41;
            }
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src4hj0beu8p6ao, 353, 13);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    gm_b16_8 t43 = gm_l1;
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s2431f6f423u2));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(20));
                    uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
                    if (!t45) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_b16_8 t46 = gm_l1;
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sn5qnjkjnmu95));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(22));
                    uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                    if (!t48) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    gm_b16_8 t49 = gm_l1;
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sktnfpvsdatf3));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(21));
                    uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                    if (!t51) {
                        goto gm_lb12;
                    }
                    goto gm_lb9;
                }
gm_lb12:;
                goto gm_lb8;
            }
gm_lb9:;
            uint8_t t52 = (uint8_t)(((((uint8_t *)&F.l3) + 48))[16] != 0);
            if (t52) {
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
                gm_l4 = t53;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t54 = gm_l1;
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s2431f6f423u2));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(20));
            uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
            if (t56) {
                gm_l12 = ((uint8_t)1);
            } else {
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 72));
                gm_l12 = t57;
            }
            uint8_t t58 = gm_l12;
            gm_l5 = t58;
            gm_b16_8 t59 = gm_l1;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sktnfpvsdatf3));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(21));
            uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
            if (t61) {
                gm_l13 = ((uint8_t)1);
            } else {
                uint8_t t62 = gm_l5;
                gm_l13 = t62;
            }
            uint8_t t63 = gm_l13;
            if (t63) {
                gm_l6 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t64 = gm_l6;
                        void * t65 = gm_l0;
                        uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)t65) + 48) + 8));
                        uint8_t t67 = (t64 < t66);
                        if (!t67) {
                            goto gm_lb13;
                        }
                        {
                            void * t68 = gm_l0;
                            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 48));
                            uint64_t t70 = GM_LD(uint64_t, ((((uint8_t *)t68) + 48) + 8));
                            uint64_t t71 = gm_l6;
                            if ((uintptr_t)t71 >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)t71, t70, gm_src4hj0beu8p6ao, 363, 26);
                            memmove(((uint8_t *)&F.l7), (((uint8_t *)t69) + (uintptr_t)t71 * 48), 48);
                            {
                                {
                                    uint8_t t72 = (uint8_t)(((((uint8_t *)&F.l7) + 16))[16] != 0);
                                    if (!t72) {
                                        goto gm_lb17;
                                    }
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                                    gm_l8 = t73;
                                    gm_b16_8 t74 = gm_l8;
                                    gm_b16_8 t75 = gm_l4;
                                    uint8_t t76 = (gm_eqbihgc69jv3(((uint8_t *)&t74), ((uint8_t *)&t75)));
                                    gm_l9 = t76;
                                    goto gm_lb16;
                                }
gm_lb17:;
                                gm_l9 = ((uint8_t)0);
                            }
gm_lb16:;
                            {
                                {
                                    uint8_t t77 = gm_l9;
                                    if (!t77) {
                                        goto gm_lb19;
                                    }
                                    gm_b16_8 t78 = gm_l1;
                                    gm_b16_8 t79;
                                    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sktnfpvsdatf3));
                                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(21));
                                    uint8_t t80 = (gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
                                    if (t80) {
                                        gm_l14 = ((uint8_t)1);
                                    } else {
                                        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                        uint8_t t83 = gm_f_is_open_a7p3eg6((void *)NULL, cx, NULL, t82);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t84 = (uint8_t)!t83;
                                        gm_l14 = t84;
                                    }
                                    uint8_t t85 = gm_l14;
                                    if (!t85) {
                                        goto gm_lb19;
                                    }
                                    {
                                        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                        gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                        gm_b0_1 t88 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t87, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjn1j0gvnde)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        {
                                            uint8_t t89 = (uint8_t)(((((uint8_t *)&F.l7) + 16))[16] != 0);
                                            if (!t89) {
                                                goto gm_lb21;
                                            }
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                                            gm_l10 = t90;
                                            gm_b16_8 t91 = gm_l10;
                                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                                            gm_b0_1 t93 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t92, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kebji8epnem)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb20;
                                        }
gm_lb21:;
                                    }
gm_lb20:;
                                    {
                                        void * t94 = gm_l0;
                                        void * t95 = (void *)(((uint8_t *)t94) + 48);
                                        uint64_t t96 = gm_l6;
                                        gm_tn3i9u3h82tga t97;
                                        gm_f_remove_j9nsdlp((void *)&t97, t95, t96);
                                    }
                                    goto gm_lb18;
                                }
gm_lb19:;
                                uint64_t t98 = gm_l6;
                                uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                if (o100) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 371, 25);
                                uint64_t t101 = r99;
                                gm_l6 = t101;
                            }
gm_lb18:;
                        }
gm_lb15:;
                    }
                }
gm_lb13:;
            }
            gm_b16_8 t102 = gm_l1;
            gm_b16_8 t103;
            GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sktnfpvsdatf3));
            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(21));
            uint8_t t104 = (gm_eq0iqiubfm7e(((uint8_t *)&t102), ((uint8_t *)&t103)));
            if (t104) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                void * t105 = gm_l0;
                void * t106 = (void *)(((uint8_t *)t105) + 48);
                gm_b16_8 t107 = gm_l2;
                gm_b16_8 t108 = gm_l4;
                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                gm_f_dupe_g9iqpvn((void *)&gm_l15, (void *)NULL, cx, NULL, t109, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjjevfvd6uv)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src4hj0beu8p6ao, 376, 49);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_th2lqbo77pvsc t112;
                GM_ST(uint8_t, (uint8_t *)&t112 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t112 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t112 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t112), t111);
                ((uint8_t *)&t112)[16] = 1;
                uint8_t t113 = gm_l5;
                gm_tn3i9u3h82tga t114;
                GM_ST(uint8_t, (uint8_t *)&t114 + 41, 0);
                GM_ST(uint16_t, (uint8_t *)&t114 + 42, 0);
                GM_ST(uint32_t, (uint8_t *)&t114 + 44, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t107);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t114 + 16), t112);
                GM_ST(uint8_t, ((uint8_t *)&t114 + 40), t113);
                gm_tftb1odc739t5 t115 = gm_f_push_rvirr6r(t106, &t114);
                gm_l16 = t115;
            }
            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src4hj0beu8p6ao, 376, 13);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb8:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 351, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_last_report_2tphe2p(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_last_report_2tphe2p F;
    gm_b16_8 gm_l0; /* err */
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* end */
    GM_STACK_CHECK_SHALLOW(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l4;
    gm_b16_8 gm_l5;
    gm_tf5srh4mp4b3i gm_l6;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snl36eh5psull));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_tf5srh4mp4b3i t4 = gm_f_rfind_ai5s2js(t2, t3);
    gm_l4 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l1 = t6;
    } else {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t10 = gm_l1;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_snl36eh5psull));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
    gm_l5 = t11;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r13; bool o14 = __builtin_add_overflow(t10, t12, &r13);
    if (o14) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 431, 17);
    uint64_t t15 = r13;
    uint64_t t16 = (uintptr_t)t15;
    uint64_t t17 = (uintptr_t)t9;
    if (t16 > t17 || t17 > (uintptr_t)t9) gm_trap_slice(t16, t17, t9, gm_src4hj0beu8p6ao, 431, 13);
    uint64_t t18 = ((uintptr_t)t9) - (uintptr_t)t15;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t8 + (uintptr_t)t15 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18));
    gm_l2 = t19;
    gm_b16_8 t20 = gm_l2;
    gm_tf5srh4mp4b3i t21 = gm_f_index_of_hdeg351(t20, ((uint8_t)10ULL));
    gm_l6 = t21;
    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t22) {
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l3 = t23;
    } else {
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l3 = t24;
    }
    void * t25 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t27 = gm_l3;
    uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t29 = (uintptr_t)t27;
    if (t28 > t29 || t29 > (uintptr_t)t26) gm_trap_slice(t28, t29, t26, gm_src4hj0beu8p6ao, 433, 12);
    uint64_t t30 = ((uintptr_t)t27) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_message_6kqdnf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0, gm_b16_8 a1) {
    struct gm_fr_message_6kqdnf1 F;
    int64_t gm_l0; /* kind */
    gm_b16_8 gm_l1; /* text_ */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tin97tt4ud0s1 t2;
        gm_f_obj_aoa7fl7((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
        int64_t t5 = gm_l0;
        gm_t8vuock3jnv59 t6;
        gm_f_int_95caidg((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t7 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t3, t4, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sujli4dml3ipq));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(7));
        gm_b16_8 t10 = gm_l1;
        gm_t8vuock3jnv59 t11;
        gm_f_text_ls49dp6((void *)&t11, t10);
        gm_b0_1 t12 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t8, t9, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t13);
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
inline __attribute__((aligned(4))) uint8_t gm_f_method_is_nfkh9v2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_method_is_nfkh9v2 F;
    gm_b16_8 gm_l0; /* body */
    gm_b16_8 gm_l1; /* method */
    gm_b16_8 gm_l3; /* id */
    gm_b16_8 gm_l4; /* m */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    {
        gm_b16_8 t8 = gm_l0;
        gm_t3irkpvggplij t9;
        gm_f_seen_b6ot3aa((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t9;
        {
            {
                uint8_t t10 = (uint8_t)(((((uint8_t *)&F.l2) + 24))[16] != 0);
                if (!t10) {
                    goto gm_lb3;
                }
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
                gm_l3 = t11;
                gm_b16_8 t12 = gm_l3;
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
                gm_b0_1 t14 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t13, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krhbibv34ui)));
                if (gm_unw.kind) {
                    goto gm_ld7_1;
                }
                goto gm_lb2;
            }
gm_lb3:;
        }
gm_lb2:;
        {
            {
                uint8_t t15 = (uint8_t)((((uint8_t *)&F.l2))[16] != 0);
                if (!t15) {
                    goto gm_lb5;
                }
                gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                gm_l4 = t16;
                gm_b16_8 t17 = gm_l4;
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_b16_8 t19 = gm_l1;
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                gm_l5 = t20;
                goto gm_lb4;
            }
gm_lb5:;
            gm_l5 = ((uint8_t)0);
        }
gm_lb4:;
        uint8_t t21 = gm_l5;
        F.ret = t21;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld7_1;
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t23 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t22);
    }
    goto gm_ls24;
gm_ld7_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t25 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t26 = gm_f_deinit_60h290e((void *)NULL, cx, NULL, t25);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls24:;
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
inline __attribute__((aligned(4))) void gm_f_next_event_notphse(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_event_notphse F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)t5);
    gm_f_lock_u5lks01(t6);
    {
        for (;;) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 48));
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
            uint8_t t11 = ((t8 == t10));
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = gm_l0;
                void * t13 = (void *)(((uint8_t *)t12) + 4);
                void * t14 = gm_l0;
                void * t15 = (void *)((uint8_t *)t14);
                gm_f_wait_k1r8nlm(t13, t15);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 8));
        uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 8) + 8));
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 48));
        if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_src4hj0beu8p6ao, 37, 10);
        memmove(((uint8_t *)&F.l1), (((uint8_t *)t17) + (uintptr_t)t20 * 40), 40);
    }
    void * t21 = gm_l0;
    void * t22 = gm_l0;
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 48));
    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
    if (o25) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 38, 5);
    uint64_t t26 = r24;
    GM_ST(uint64_t, (((uint8_t *)t21) + 48), t26);
    void * t27 = gm_l0;
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 48));
    void * t29 = gm_l0;
    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 8) + 8));
    uint8_t t31 = ((t28 == t30));
    if (t31) {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 8);
        gm_f_clear_ptgum12(t33);
        void * t34 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t34) + 48), ((uint64_t)0ULL));
    }
    void * t35 = gm_l0;
    void * t36 = (void *)((uint8_t *)t35);
    gm_f_unlock_9f1vcl6(t36);
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tumavt1402akl gm_f_next_event_notphse_c(void * a0) { gm_tumavt1402akl gm_r; gm_f_next_event_notphse(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_post_u7g5gcu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_post_u7g5gcu_c(void * a0, gm_tumavt1402akl a1) { gm_f_post_u7g5gcu(a0, &a1); }
__attribute__((aligned(4))) void gm_f_read_worker_kl9k6j8(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_worker_kl9k6j8 F;
    void * gm_l0; /* w */
    gm_te0mgnjdphs16 gm_l4; /* chunk */
    gm_b16_8 gm_l5; /* data */
    gm_b16_8 gm_l6; /* body */
    gm_b16_8 gm_l7; /* keep */
    gm_tqnlpelep1582 gm_l9; /* status */
    gm_tsoiri44d0i6q gm_l10; /* st */
    GM_STACK_CHECK(&F, 1128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_thdrvp9q0u7b5 gm_l11;
    gm_tlbmih0s72in3 gm_l12;
    gm_thdrvp9q0u7b5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tlmjd3nndlf3r gm_l18;
    {
        gm_t3mf58gv9kf3n t14 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t15 = t14;
        gm_node wn16 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t15, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b0_1 t17 = gm_f_system_io_mcjcl9m();
        gm_b0_1 t18 = GM_ZERO(gm_b0_1);
        gm_node wn19 = {.eff = 1018230276, .kind = GM_DECL, .clauses = gm_hd3g3ujqvah4, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn16)};
        gm_node wn20 = {.eff = 1579066750, .kind = GM_DECL, .clauses = gm_hdf4dspmusp5, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn19)};
        gm_node wn21 = {.eff = 1087599154, .kind = GM_DECL, .clauses = gm_hdvslis4o40u, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn20)};
        gm_node wn22 = {.eff = 2126215853, .kind = GM_DECL, .clauses = gm_hdu8v6n5i2qd, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn21)};
        gm_node wn23 = {.eff = 163658498, .kind = GM_DECL, .clauses = gm_hdffkeesmui4, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn22)};
        gm_node wn24 = {.eff = 2119230, .kind = GM_DECL, .clauses = gm_hdaqemfrshlp, .env = (void *)&t18, .hctx = (&wn16), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn23)};
        gm_b0_1 t25 = gm_f_system_output_eg72lom();
        gm_b0_1 t26 = GM_ZERO(gm_b0_1);
        gm_node wn27 = {.eff = 515083575, .kind = GM_DECL, .clauses = gm_hdu50j4i95ke, .env = (void *)&t26, .hctx = (&wn24), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn24)};
        gm_node wn28 = {.eff = 2063121243, .kind = GM_DECL, .clauses = gm_hddfeetqn95b, .env = (void *)&t26, .hctx = (&wn24), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn27)};
        {
            gm_trqfvmjtt4sij t31;
            gm_f_list_uvfu1k1((void *)&t31, (void *)NULL, (&wn28), NULL);
            if (gm_unw.kind) {
                goto gm_ll30;
            }
            F.l1 = t31;
        }
        {
            gm_trqfvmjtt4sij t32;
            gm_f_list_uvfu1k1((void *)&t32, (void *)NULL, (&wn28), NULL);
            if (gm_unw.kind) {
                goto gm_ll30;
            }
            F.l2 = t32;
        }
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb2;
                }
                {
                    {
                        void * t33 = gm_l0;
                        gm_tm98ap4112hee t34 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t33) + 8));
                        void * t35 = (void *)((uint8_t *)&F.l3);
                        uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t37 = (uintptr_t)((uint64_t)65536ULL);
                        if (t36 > t37 || t37 > (uintptr_t)((uint64_t)65536ULL)) gm_trap_slice(t36, t37, ((uint64_t)65536ULL), gm_src4hj0beu8p6ao, 76, 36);
                        uint64_t t38 = ((uintptr_t)((uint64_t)65536ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t35 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
                        gm_node *pn40 = gm_find((&wn28), 2119230);
                        if (!pn40) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 76, 23);
                        gm_tlbmih0s72in3 t41;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn40->clauses[3])((void *)&t41, pn40->env, pn40->hctx, pn40, t34, t39);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                        gm_l12 = t41;
                    }
                    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (t42) {
                        memmove(((uint8_t *)&gm_l11), (((uint8_t *)&gm_l12) + 8), 24);
                    } else {
                        goto gm_lb2;
                    }
                    {
                        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l11), 24);
                    }
                    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
                    if (t43) {
                        gm_te0mgnjdphs16 t44 = GM_LD(gm_te0mgnjdphs16, ((uint8_t *)&gm_l13));
                        gm_l4 = t44;
                    } else {
                        goto gm_lb2;
                    }
                    void * t45 = (void *)((uint8_t *)&F.l3);
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t48 = (uintptr_t)t46;
                    if (t47 > t48 || t48 > (uintptr_t)((uint64_t)65536ULL)) gm_trap_slice(t47, t48, ((uint64_t)65536ULL), gm_src4hj0beu8p6ao, 77, 21);
                    uint64_t t49 = ((uintptr_t)t46) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49));
                    gm_l5 = t50;
                    {
                        {
                            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)0ULL));
                            if (!t52) {
                                goto gm_lb6;
                            }
                            void * t53 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t54 = gm_l5;
                            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                            gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t53, t55);
                            gm_l14 = t56;
                            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src4hj0beu8p6ao, 80, 21);
                            {
                                for (;;) {
                                    void * t58 = (void *)((uint8_t *)&F.l1);
                                    gm_f_take_frame_nf67ep0((void *)&gm_l15, (void *)NULL, (&wn28), NULL, t58);
                                    if (gm_unw.kind) {
                                        goto gm_ll30;
                                    }
                                    uint8_t t59 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                    if (!t59) {
                                        goto gm_lb7;
                                    }
                                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                    gm_l6 = t60;
                                    {
                                        void * t61 = gm_l0;
                                        void * t62 = GM_LD(void *, ((uint8_t *)t61));
                                        void * t63 = gm_l0;
                                        uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t63) + 16));
                                        gm_b16_8 t65 = gm_l6;
                                        gm_tiilmfd16v3fc t66;
                                        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t64);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
                                        gm_tumavt1402akl t67;
                                        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t67 + 32, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t67 + 8), t66);
                                        gm_f_post_u7g5gcu(t62, &t67);
                                    }
gm_lb9:;
                                }
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)1ULL));
                            if (!t69) {
                                goto gm_lb10;
                            }
                            gm_b16_8 t70 = gm_l5;
                            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                            gm_pf0nmuupvugn((&wn28), t71, gm_src4hj0beu8p6ao, 84, 21);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            void * t72 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t73 = gm_l5;
                            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                            gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t72, t74);
                            gm_l16 = t75;
                            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src4hj0beu8p6ao, 85, 21);
                            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                            uint64_t r78; bool o79 = __builtin_mul_overflow(((uint64_t)2ULL), ((uint64_t)16384ULL), &r78);
                            if (o79) gm_trap("integer overflow in `*`", gm_src4hj0beu8p6ao, 86, 41);
                            uint64_t t80 = r78;
                            uint8_t t81 = (t77 > t80);
                            if (t81) {
                                void * t82 = GM_LD(void *, ((uint8_t *)&F.l2));
                                uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                uint64_t r85; bool o86 = __builtin_sub_overflow(t84, ((uint64_t)16384ULL), &r85);
                                if (o86) gm_trap("integer overflow in `-`", gm_src4hj0beu8p6ao, 87, 44);
                                uint64_t t87 = r85;
                                uint64_t t88 = (uintptr_t)t87;
                                uint64_t t89 = (uintptr_t)t83;
                                if (t88 > t89 || t89 > (uintptr_t)t83) gm_trap_slice(t88, t89, t83, gm_src4hj0beu8p6ao, 87, 33);
                                uint64_t t90 = ((uintptr_t)t83) - (uintptr_t)t87;
                                gm_b16_8 t91;
                                GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t82 + (uintptr_t)t87 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(t90));
                                gm_l7 = t91;
                                {
                                    gm_trqfvmjtt4sij t92;
                                    gm_f_list_uvfu1k1((void *)&t92, (void *)NULL, (&wn28), NULL);
                                    if (gm_unw.kind) {
                                        goto gm_ll30;
                                    }
                                    F.l8 = t92;
                                }
                                void * t93 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t94 = gm_l7;
                                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                gm_tftb1odc739t5 t96 = gm_f_extend_0cs85gs(t93, t95);
                                gm_l17 = t96;
                                uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src4hj0beu8p6ao, 89, 25);
                                void * t98 = (void *)((uint8_t *)&F.l2);
                                gm_f_deinit_r70pji5(t98);
                                {
                                    memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l8), 40);
                                }
                            }
                            goto gm_lb5;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 78, 13);
                    }
gm_lb5:;
                }
gm_lb4:;
            }
        }
gm_lb2:;
        gm_tqnlpelep1582 t99;
        memset(&t99, 0, sizeof t99);
        gm_l9 = t99;
        {
            {
                void * t100 = gm_l0;
                gm_tm98ap4112hee t101 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t100) + 8));
                gm_node *pn102 = gm_find((&wn28), 2119230);
                if (!pn102) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 97, 18);
                gm_tlmjd3nndlf3r t103;
                t103 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn102->clauses[4])(pn102->env, pn102->hctx, pn102, t101);
                if (gm_unw.kind) {
                    goto gm_ll30;
                }
                gm_l18 = t103;
                uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t104) {
                    goto gm_lb12;
                }
                gm_tsoiri44d0i6q t105 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&gm_l18) + 4));
                gm_l10 = t105;
                gm_tsoiri44d0i6q t106 = gm_l10;
                gm_tqnlpelep1582 t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 10, 0);
                GM_ST(gm_tsoiri44d0i6q, ((uint8_t *)&t107), t106);
                ((uint8_t *)&t107)[8] = 1;
                gm_l9 = t107;
                goto gm_lb11;
            }
gm_lb12:;
        }
gm_lb11:;
        {
            void * t108 = gm_l0;
            void * t109 = GM_LD(void *, ((uint8_t *)t108));
            void * t110 = gm_l0;
            uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)t110) + 16));
            gm_tqnlpelep1582 t112 = gm_l9;
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_tsqm4sdtus60a t114;
            GM_ST(uint32_t, ((uint8_t *)&t114 + 0), t111);
            GM_ST(gm_tqnlpelep1582, ((uint8_t *)&t114 + 4), t112);
            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t113);
            gm_tumavt1402akl t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)3ULL));
            GM_ST(gm_tsqm4sdtus60a, ((uint8_t *)&t115 + 8), t114);
            gm_f_post_u7g5gcu(t109, &t115);
        }
    }
    goto gm_ls116;
gm_ll30:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we29; }
    goto gm_lad;
gm_ls116:;
gm_lb1:;
we29:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_respond_raw_error_fc9dcul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_respond_raw_error_fc9dcul F;
    gm_b16_8 gm_l0; /* id */
    gm_b16_8 gm_l1; /* message_ */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tit4ajsn3pv91 gm_l3;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_f_parse_invslf8((void *)&gm_l3, (void *)NULL, cx, NULL, t4, ((uint64_t)512ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t5) {
            memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 72);
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        {
            gm_b16_8 t6 = gm_l1;
            gm_b0_1 t7 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&F.l2)), ((int64_t)-32603LL), t6);
            if (gm_unw.kind) {
                goto gm_ld3_1;
            }
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_l8j91qd(t8);
    }
    goto gm_ls9;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t10 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_l8j91qd(t10);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls9:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_restart_62t9uv6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_restart_62t9uv6 F;
    void * gm_l0; /* sup */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t7 = gm_l0;
    uint8_t t8 = gm_f_start_lnda69t((void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!t8;
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 48));
    gm_l1 = t11;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t17 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t15) + (uintptr_t)t17 * 48), 48);
                        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
                        uint8_t t20 = gm_f_is_initialize_4al2pub((void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t20) {
                            void * t21 = gm_l0;
                            GM_ST(uint8_t, (((uint8_t *)t21) + 46), ((uint8_t)1));
                        }
                        void * t22 = gm_l0;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        gm_b0_1 t25 = gm_f_to_worker_bu6lrlk((void *)NULL, cx, NULL, t22, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 244, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l2;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l2 = t29;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) void gm_f_seen_b6ot3aa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_seen_b6ot3aa F;
    gm_b16_8 gm_l0; /* body */
    gm_b16_8 gm_l3; /* m */
    gm_t8vuock3jnv59 gm_l4; /* id */
    gm_t8vuock3jnv59 gm_l5; /* params */
    gm_b16_8 gm_l6; /* u */
    gm_b16_8 gm_l7; /* changes */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t8vuock3jnv59 gm_l10; /* c */
    GM_STACK_CHECK(&F, 1180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tit4ajsn3pv91 gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_t3ph3kbiqt1ah gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    uint8_t gm_l19;
    gm_t3ph3kbiqt1ah gm_l20;
    {
        {
            gm_b16_8 t20 = gm_l0;
            gm_f_parse_invslf8((void *)&gm_l11, (void *)NULL, cx, NULL, t20, ((uint64_t)512ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (t21) {
            memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l11) + 8), 72);
        } else {
            gm_th2lqbo77pvsc t22;
            memset(&t22, 0, sizeof t22);
            gm_th2lqbo77pvsc t23;
            memset(&t23, 0, sizeof t23);
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 76, 0);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 0), t22);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 24), t23);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 48), t24);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t3irkpvggplij *)gm_out) + 72), ((uint8_t)0));
            goto gm_fdone;
        }
        {
            gm_th2lqbo77pvsc t25;
            memset(&t25, 0, sizeof t25);
            gm_th2lqbo77pvsc t26;
            memset(&t26, 0, sizeof t26);
            gm_th2lqbo77pvsc t27;
            memset(&t27, 0, sizeof t27);
            GM_ST(uint8_t, (uint8_t *)&F.l2 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&F.l2 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&F.l2 + 76, 0);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l2 + 0), t25);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l2 + 24), t26);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l2 + 48), t27);
            GM_ST(uint8_t, ((uint8_t *)&F.l2 + 72), ((uint8_t)0));
        }
        {
            {
                {
                    gm_b16_8 t28;
                    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sg7of7eks8a5u));
                    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(6));
                    gm_t8vuock3jnv59 t29;
                    gm_f_field_7904fsq((void *)&t29, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&F.l1)), t28);
                    gm_f_as_str_9rhg9v0((void *)&gm_l12, &t29);
                }
                uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
                if (!t30) {
                    goto gm_lb3;
                }
                gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                gm_l3 = t31;
                {
                    gm_b16_8 t32 = gm_l3;
                    gm_f_dupe_g9iqpvn((void *)&gm_l13, (void *)NULL, cx, NULL, t32, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7ik3fa1b9i)));
                    if (gm_unw.kind) {
                        goto gm_ld19_1;
                    }
                }
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src4hj0beu8p6ao, 288, 60);
                {
                    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                    gm_th2lqbo77pvsc t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t35), t34);
                    ((uint8_t *)&t35)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l2), t35);
                }
                goto gm_lb2;
            }
gm_lb3:;
        }
gm_lb2:;
        {
            {
                {
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sq3gi0chb3cs2));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
                    gm_t8vuock3jnv59 t37 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&F.l1));
                    gm_f_get_fjdb5ou((void *)&gm_l14, &t37, t36);
                }
                uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l14))[24] != 0);
                if (!t38) {
                    goto gm_lb5;
                }
                {
                    memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l14), 24);
                }
                {
                    gm_b16_8 t39 = gm_f_to_text_2cum4i7((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l4)));
                    if (gm_unw.kind) {
                        goto gm_ld19_1;
                    }
                    gm_f_dupe_g9iqpvn((void *)&gm_l15, (void *)NULL, cx, NULL, t39, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq4dnvi7inu)));
                    if (gm_unw.kind) {
                        goto gm_ld19_1;
                    }
                }
                uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src4hj0beu8p6ao, 289, 48);
                {
                    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                    gm_th2lqbo77pvsc t42;
                    GM_ST(uint8_t, (uint8_t *)&t42 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t42 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t42 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t42), t41);
                    ((uint8_t *)&t42)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 24), t42);
                }
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
        {
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sfkmkcagsnqnb));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(6));
            gm_t8vuock3jnv59 t44 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&F.l1));
            gm_f_field_7904fsq((void *)&gm_l5, &t44, t43);
        }
        {
            {
                {
                    gm_b16_8 t45;
                    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s4j3ecogbjkvn));
                    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(12));
                    gm_b16_8 t46;
                    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sbo52miqad506));
                    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
                    gm_t8vuock3jnv59 t47;
                    gm_f_path2_enc7vaj((void *)&t47, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l5)), t45, t46);
                    gm_f_as_str_9rhg9v0((void *)&gm_l16, &t47);
                }
                uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                if (!t48) {
                    goto gm_lb7;
                }
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                gm_l6 = t49;
                {
                    gm_b16_8 t50 = gm_l6;
                    gm_f_dupe_g9iqpvn((void *)&gm_l17, (void *)NULL, cx, NULL, t50, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpqmkpiene)));
                    if (gm_unw.kind) {
                        goto gm_ld19_1;
                    }
                }
                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4hj0beu8p6ao, 291, 68);
                {
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                    gm_th2lqbo77pvsc t53;
                    GM_ST(uint8_t, (uint8_t *)&t53 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t53 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t53 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t53), t52);
                    ((uint8_t *)&t53)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 48), t53);
                }
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            {
                {
                    gm_b16_8 t54;
                    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sunu1gosuevl4));
                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(14));
                    gm_t8vuock3jnv59 t55;
                    gm_f_field_7904fsq((void *)&t55, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l5)), t54);
                    gm_f_as_array_nfvj8pk((void *)&gm_l18, &t55);
                }
                uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                if (!t56) {
                    goto gm_lb9;
                }
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                gm_l7 = t57;
                GM_ST(uint8_t, (((uint8_t *)&F.l2) + 72), ((uint8_t)1));
                gm_b16_8 t58 = gm_l7;
                gm_l8 = t58;
                gm_l9 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint8_t t61 = (t59 < t60);
                        if (!t61) {
                            goto gm_lb10;
                        }
                        {
                            void * t62 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint64_t t64 = gm_l9;
                            {
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l10), (((uint8_t *)t62) + (uintptr_t)t64 * 24), 24);
                                    }
                                    {
                                        gm_b16_8 t65;
                                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sl4ns6hobicht));
                                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(5));
                                        gm_t8vuock3jnv59 t66 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l10));
                                        gm_f_get_fjdb5ou((void *)&gm_l20, &t66, t65);
                                    }
                                    {
                                        {
                                            uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l20))[24] != 0);
                                            if (t67) {
                                                goto gm_lb16;
                                            }
                                            gm_l19 = ((uint8_t)0);
                                            goto gm_lb15;
                                        }
gm_lb16:;
                                        {
                                            gm_l19 = ((uint8_t)1);
                                            goto gm_lb15;
                                        }
gm_lb17:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 295, 16);
                                    }
gm_lb15:;
                                    uint8_t t68 = gm_l19;
                                    if (t68) {
                                        GM_ST(uint8_t, (((uint8_t *)&F.l2) + 72), ((uint8_t)0));
                                    }
                                    goto gm_lb13;
                                }
gm_lb14:;
                                gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 294, 9);
                            }
gm_lb13:;
                        }
gm_lb12:;
                        uint64_t t69 = gm_l9;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l9 = t72;
                    }
                }
gm_lb10:;
                goto gm_lb8;
            }
gm_lb9:;
        }
gm_lb8:;
        {
            memmove(&F.ret, ((uint8_t *)&F.l2), 80);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld19_1;
        }
    }
    {
        void * t73 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_l8j91qd(t73);
    }
    goto gm_ls74;
gm_ld19_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t75 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_l8j91qd(t75);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls74:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_start_lnda69t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_start_lnda69t F;
    void * gm_l0; /* sup */
    gm_tm98ap4112hee gm_l1; /* child */
    void * gm_l2; /* w */
    GM_STACK_CHECK(&F, 532u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t62ebbi2i5rkd gm_l3;
    gm_tq7s98ihonkju gm_l4;
    gm_tm98ap4112hee gm_l5;
    gm_t62ebbi2i5rkd gm_l6;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 8));
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)gm_clslitrb510));
        gm_th2lqbo77pvsc t6;
        memset(&t6, 0, sizeof t6);
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)2ULL));
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)2ULL));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
        gm_t32dckm4jfarl t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 3, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 52, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t7);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 2), t9);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t10 + 8), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 32), t5);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 48), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t10 + 49), ((uint8_t)0));
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t10 + 56), t6);
        gm_node *pn11 = gm_find(cx, 2119230);
        if (!pn11) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 224, 14);
        gm_t62ebbi2i5rkd t12;
        t12 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn11->clauses[0])(pn11->env, pn11->hctx, pn11, t3, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t12;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t13) {
        gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l3) + 8));
        gm_l1 = t14;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 40));
    uint32_t r18; bool o19 = __builtin_add_overflow(t17, ((uint32_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 225, 5);
    uint32_t t20 = r18;
    GM_ST(uint32_t, (((uint8_t *)t15) + 40), t20);
    void * t21 = gm_l0;
    gm_tm98ap4112hee t22 = gm_l1;
    gm_tppo2hmut88dq t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[8] = 1;
    GM_ST(gm_tppo2hmut88dq, (((uint8_t *)t21) + 24), t23);
    void * t24 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t24) + 44), ((uint8_t)1));
    void * t25 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t25) + 45), ((uint8_t)1));
    void * t26 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t26) + 46), ((uint8_t)0));
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, ((uint8_t *)t27));
        gm_tm98ap4112hee t29 = gm_l1;
        void * t30 = gm_l0;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 40));
        gm_tci6c4can0r43 t32;
        GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
        GM_ST(void *, ((uint8_t *)&t32 + 0), t28);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t32 + 8), t29);
        GM_ST(uint32_t, ((uint8_t *)&t32 + 16), t31);
        gm_tq7s98ihonkju t33 = gm_f_new_t835u2t((void *)NULL, cx, NULL, &t32, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgh95sj78n8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t33;
    }
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src4hj0beu8p6ao, 230, 10);
    void * t35 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    gm_l2 = t35;
    void * t36 = gm_l2;
    gm_t62ebbi2i5rkd t37 = gm_f_spawn_ic9k9n0(((void *)gm_f_read_worker_kl9k6j8), t36);
    gm_l6 = t37;
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t38) {
        gm_tm98ap4112hee t39 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l6) + 8));
        gm_l5 = t39;
    } else {
        gm_tm98ap4112hee t40 = gm_l1;
        gm_node *pn41 = gm_find(cx, 2119230);
        if (!pn41) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 232, 13);
        gm_ttln0d0hd70en t42;
        t42 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn41->clauses[6])(pn41->env, pn41->hctx, pn41, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t43 = gm_l1;
        gm_node *pn44 = gm_find(cx, 2119230);
        if (!pn44) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 233, 13);
        gm_tlmjd3nndlf3r t45;
        t45 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn44->clauses[4])(pn44->env, pn44->hctx, pn44, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t46 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t46) + 44), ((uint8_t)0));
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_tm98ap4112hee t47 = gm_l5;
    F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) uint8_t gm_f_supervise_hvohndn(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_supervise_hvohndn F;
    void * gm_l0; /* q */
    void * gm_l1; /* box */
    void * gm_l2; /* link */
    gm_t3ujhu6qph7de gm_l6; /* m */
    gm_tm98ap4112hee gm_l7; /* c */
    uint32_t gm_l8; /* gen */
    gm_b16_8 gm_l9; /* body */
    uint32_t gm_l10; /* gen */
    gm_tqnlpelep1582 gm_l11; /* status */
    gm_b16_8 gm_l12; /* err */
    uint8_t gm_l13; /* code */
    GM_STACK_CHECK(&F, 1659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tq7s98ihonkju gm_l14;
    gm_tq7s98ihonkju gm_l15;
    gm_tm98ap4112hee gm_l16;
    gm_t62ebbi2i5rkd gm_l17;
    gm_tq7s98ihonkju gm_l18;
    gm_tm98ap4112hee gm_l19;
    gm_t62ebbi2i5rkd gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tumavt1402akl gm_l24;
    {
        gm_t88rcucg1b7uk t12 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
        gm_t88rcucg1b7uk t13 = GM_CONV(gm_t88rcucg1b7uk, gm_f_condvar_rp5e3qr());
        gm_trqfvmjtt4sij t14;
        gm_f_list_kr0h52q((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tapvs478p7gjp t15;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 0), t12);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 4), t13);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t15 + 8), t14);
        GM_ST(uint64_t, ((uint8_t *)&t15 + 48), ((uint64_t)0ULL));
        gm_tq7s98ihonkju t16 = gm_f_new_d0cb7fc((void *)NULL, cx, NULL, &t15, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koalr8ndd0a)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t16;
    }
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src4hj0beu8p6ao, 164, 10);
    void * t18 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
    gm_l0 = t18;
    {
        gm_t6cm8mrseh8uc t19;
        gm_f_inbox_h89r727((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t20 = gm_f_new_7f4cigf((void *)NULL, cx, NULL, &t19, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k91s9e2j9g7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t20;
    }
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src4hj0beu8p6ao, 165, 12);
    void * t22 = GM_LD(void *, (((uint8_t *)&gm_l15) + 8));
    gm_l1 = t22;
    void * t23 = gm_l1;
    gm_t62ebbi2i5rkd t24 = gm_f_spawn_stdjo94(((void *)gm_f_reader_4e2j89o), t23);
    gm_l17 = t24;
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (t25) {
        gm_tm98ap4112hee t26 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l17) + 8));
        gm_l16 = t26;
    } else {
        if (gm_pf454qfrpt6r(cx, gm_s7bj0jma5l2mg, 38, gm_src4hj0beu8p6ao, 167, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)1ULL);
        goto gm_fret;
    }
    gm_tm98ap4112hee t27 = gm_l16;
    {
        void * t28 = gm_l1;
        void * t29 = gm_l0;
        gm_t3mf58gv9kf3n t30;
        GM_ST(void *, ((uint8_t *)&t30 + 0), t28);
        GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
        gm_tq7s98ihonkju t31 = gm_f_new_umo8ulm((void *)NULL, cx, NULL, t30, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knvhkf6cslq)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t31;
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src4hj0beu8p6ao, 170, 13);
    void * t33 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
    gm_l2 = t33;
    void * t34 = gm_l2;
    gm_t62ebbi2i5rkd t35 = gm_f_spawn_0kkmakv(((void *)gm_f_forward_client_h5f058d), t34);
    gm_l20 = t35;
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t36) {
        gm_tm98ap4112hee t37 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l20) + 8));
        gm_l19 = t37;
    } else {
        if (gm_pf454qfrpt6r(cx, gm_s7bj0jma5l2mg, 38, gm_src4hj0beu8p6ao, 172, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)1ULL);
        goto gm_fret;
    }
    gm_tm98ap4112hee t38 = gm_l19;
    {
        gm_trqfvmjtt4sij t39;
        gm_f_list_epvducg((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t39;
    }
    void * t40 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t41 = gm_f_executable_lr5dqch((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t42 = gm_f_push_3ef746b(t40, t41);
    gm_l21 = t42;
    uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src4hj0beu8p6ao, 176, 5);
    void * t44 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sdjba9ailb8m1));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
    gm_l22 = t46;
    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src4hj0beu8p6ao, 177, 5);
    void * t48 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s352761194jbs));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t50 = gm_f_push_3ef746b(t48, t49);
    gm_l23 = t50;
    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src4hj0beu8p6ao, 178, 5);
    {
        void * t52 = gm_l0;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_tppo2hmut88dq t55;
        memset(&t55, 0, sizeof t55);
        gm_trqfvmjtt4sij t56;
        gm_f_list_nb7om6u((void *)&t56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t57;
        gm_f_list_otmlne2((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttgknnvmqfhok t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 132, 0);
        GM_ST(void *, ((uint8_t *)&t58 + 0), t52);
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t54);
        GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&t58 + 24), t55);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 40), ((uint32_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t58 + 44), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t58 + 45), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t58 + 46), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t58 + 47), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t58 + 48), t56);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t58 + 88), t57);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 128), ((uint32_t)0ULL));
        F.l4 = t58;
    }
    void * t59 = (void *)((uint8_t *)&F.l4);
    uint8_t t60 = gm_f_start_lnda69t((void *)NULL, cx, NULL, t59);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t61 = (uint8_t)!t60;
    if (t61) {
        if (gm_pf454qfrpt6r(cx, gm_spqmq1ln67me5, 78, gm_src4hj0beu8p6ao, 182, 9)) {
            goto gm_lad;
        }
        gm_tmnkfo49014kk t62;
        gm_f_server_6g9m6gg((void *)&t62, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t62;
        void * t63 = (void *)((uint8_t *)&F.l5);
        void * t64 = gm_l1;
        uint8_t t65 = gm_f_run_rkae3vt((void *)NULL, cx, NULL, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t65;
        goto gm_fret;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t66 = gm_l0;
                gm_f_next_event_notphse((void *)&gm_l24, t66);
                {
                    {
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t68 = (uint8_t)(t67 == ((uint8_t)0ULL));
                        if (!t68) {
                            goto gm_lb5;
                        }
                        {
                            memmove(((uint8_t *)&gm_l6), (((uint8_t *)&gm_l24) + 8), 24);
                        }
                        {
                            void * t69 = (void *)((uint8_t *)&F.l4);
                            gm_b0_1 t70 = gm_f_from_client_liluprp((void *)NULL, cx, NULL, t69, &(*(gm_t3ujhu6qph7de *)((uint8_t *)&gm_l6)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)1ULL));
                        if (!t72) {
                            goto gm_lb6;
                        }
                        GM_ST(uint8_t, (((uint8_t *)&F.l4) + 47), ((uint8_t)1));
                        {
                            {
                                uint8_t t73 = (uint8_t)(((((uint8_t *)&F.l4) + 24))[8] != 0);
                                if (!t73) {
                                    goto gm_lb8;
                                }
                                gm_tm98ap4112hee t74 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l4) + 24));
                                gm_l7 = t74;
                                uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 44));
                                if (t75) {
                                    gm_tm98ap4112hee t76 = gm_l7;
                                    gm_node *pn77 = gm_find(cx, 2119230);
                                    if (!pn77) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 191, 52);
                                    gm_b0_1 t78;
                                    t78 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn77->clauses[2])(pn77->env, pn77->hctx, pn77, t76);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                        }
gm_lb7:;
                        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 44));
                        uint8_t t80 = (uint8_t)!t79;
                        if (t80) {
                            F.ret = ((uint8_t)1ULL);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)2ULL));
                        if (!t82) {
                            goto gm_lb9;
                        }
                        uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 8));
                        gm_l8 = t83;
                        gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l24) + 8) + 8));
                        gm_l9 = t84;
                        uint32_t t85 = gm_l8;
                        uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 40));
                        uint8_t t87 = ((t85 == t86));
                        if (t87) {
                            void * t88 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t89 = gm_l9;
                            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                            gm_b0_1 t91 = gm_f_from_worker_quo4ehg((void *)NULL, cx, NULL, t88, t90);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        gm_b16_8 t92 = gm_l9;
                        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                        gm_b0_1 t94 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t93, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kihs3be4rv6)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb9:;
                    {
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)3ULL));
                        if (!t96) {
                            goto gm_lb10;
                        }
                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 8));
                        gm_l10 = t97;
                        gm_tqnlpelep1582 t98 = GM_LD(gm_tqnlpelep1582, ((((uint8_t *)&gm_l24) + 8) + 4));
                        gm_l11 = t98;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l24) + 8) + 16));
                        gm_l12 = t99;
                        uint32_t t100 = gm_l10;
                        uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 40));
                        uint8_t t102 = (!(t100 == t101));
                        if (t102) {
                            goto gm_lb3;
                        }
                        GM_ST(uint8_t, (((uint8_t *)&F.l4) + 44), ((uint8_t)0));
                        gm_tqnlpelep1582 t103 = gm_l11;
                        uint8_t t104 = gm_f_exit_code_0nbqupf(t103);
                        gm_l13 = t104;
                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 47));
                        if (t105) {
                            uint8_t t106 = gm_l13;
                            F.ret = t106;
                            goto gm_fret;
                        }
                        void * t107 = (void *)((uint8_t *)&F.l4);
                        gm_tqnlpelep1582 t108 = gm_l11;
                        gm_b16_8 t109 = gm_l12;
                        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                        gm_b0_1 t111 = gm_f_crashed_vh3npqh((void *)NULL, cx, NULL, t107, t108, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t112 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 128));
                        uint8_t t113 = (t112 >= ((uint32_t)50ULL));
                        if (t113) {
                            gm_b16_8 t114;
                            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sl4gq9e3k2i6t));
                            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(18));
                            gm_b16_8 t115;
                            GM_ST(void *, (uint8_t *)&t115, (void *)(gm_so1ub93q801tl));
                            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(64));
                            gm_t8vuock3jnv59 t116;
                            gm_f_message_6kqdnf1((void *)&t116, (void *)NULL, cx, NULL, ((int64_t)1LL), t115);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b0_1 t117 = gm_f_notify_ls6n69d((void *)NULL, cx, NULL, t114, &t116);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = ((uint8_t)1ULL);
                            goto gm_fret;
                        }
                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 45));
                        uint8_t t119 = (uint8_t)!t118;
                        if (t119) {
                            void * t120 = (void *)((uint8_t *)&F.l4);
                            uint8_t t121 = gm_f_restart_62t9uv6((void *)NULL, cx, NULL, t120);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src4hj0beu8p6ao, 187, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
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
__attribute__((aligned(4))) void gm_f_take_frame_nf67ep0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_take_frame_nf67ep0 F;
    void * gm_l0; /* pending */
    gm_b16_8 gm_l1; /* text */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* start */
    gm_b16_8 gm_l6; /* body */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t3 = gm_l0;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sf4qi8ut6u3p1));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(4));
    gm_tf5srh4mp4b3i t8 = gm_f_find_nab29co(t6, t7);
    gm_l8 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        gm_l2 = t10;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    F.l3 = ((uint64_t)0ULL);
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t13 = gm_l2;
    uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t15 = (uintptr_t)t13;
    if (t14 > t15 || t15 > (uintptr_t)t12) gm_trap_slice(t14, t15, t12, gm_src4hj0beu8p6ao, 108, 27);
    uint64_t t16 = ((uintptr_t)t13) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t11 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
    gm_l4 = t17;
    {
        gm_node wn18 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlm44rh1m4cu, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t21 = gm_l4;
        gm_b0_1 t22 = gm_f_lines_9uns0k2_s0g5ifv((void *)NULL, (&wn18), NULL, t21);
        if (gm_unw.kind) {
            goto gm_ll20;
        }
    }
    goto gm_ls23;
gm_ll20:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we19; }
    goto gm_lad;
gm_ls23:;
gm_lb1:;
we19:;
    uint64_t t24 = gm_l2;
    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)4ULL), &r25);
    if (o26) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 114, 14);
    uint64_t t27 = r25;
    gm_l5 = t27;
    void * t28 = gm_l0;
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 8));
    uint64_t t30 = gm_l5;
    uint64_t r31; bool o32 = __builtin_sub_overflow(t29, t30, &r31);
    if (o32) gm_trap("integer overflow in `-`", gm_src4hj0beu8p6ao, 115, 8);
    uint64_t t33 = r31;
    uint64_t t34 = F.l3;
    uint8_t t35 = (t33 < t34);
    if (t35) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)t36));
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
        uint64_t t39 = gm_l5;
        uint64_t t40 = gm_l5;
        uint64_t t41 = F.l3;
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 116, 39);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t39;
        uint64_t t46 = (uintptr_t)t44;
        if (t45 > t46 || t46 > (uintptr_t)t38) gm_trap_slice(t45, t46, t38, gm_src4hj0beu8p6ao, 116, 18);
        uint64_t t47 = ((uintptr_t)t44) - (uintptr_t)t39;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t37 + (uintptr_t)t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_f_dupe_g9iqpvn((void *)&gm_l9, (void *)NULL, cx, NULL, t49, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kcdn8q7rsvc)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src4hj0beu8p6ao, 116, 13);
    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_l6 = t51;
    {
        gm_trqfvmjtt4sij t52;
        gm_f_list_uvfu1k1((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t52;
    }
    void * t53 = (void *)((uint8_t *)&F.l7);
    void * t54 = gm_l0;
    void * t55 = GM_LD(void *, ((uint8_t *)t54));
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
    uint64_t t57 = gm_l5;
    uint64_t t58 = F.l3;
    uint64_t r59; bool o60 = __builtin_add_overflow(t57, t58, &r59);
    if (o60) gm_trap("integer overflow in `+`", gm_src4hj0beu8p6ao, 118, 31);
    uint64_t t61 = r59;
    uint64_t t62 = (uintptr_t)t61;
    uint64_t t63 = (uintptr_t)t56;
    if (t62 > t63 || t63 > (uintptr_t)t56) gm_trap_slice(t62, t63, t56, gm_src4hj0beu8p6ao, 118, 17);
    uint64_t t64 = ((uintptr_t)t56) - (uintptr_t)t61;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t55 + (uintptr_t)t61 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64));
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
    gm_tftb1odc739t5 t67 = gm_f_extend_0cs85gs(t53, t66);
    gm_l10 = t67;
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src4hj0beu8p6ao, 118, 5);
    void * t69 = gm_l0;
    gm_f_deinit_r70pji5(t69);
    {
        void * t70 = gm_l0;
        memmove(((uint8_t *)t70), ((uint8_t *)&F.l7), 40);
    }
    {
        gm_b16_8 t71 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t71);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_to_worker_bu6lrlk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_to_worker_bu6lrlk F;
    void * gm_l0; /* sup */
    gm_b16_8 gm_l1; /* body */
    gm_tm98ap4112hee gm_l2; /* c */
    gm_b16_8 gm_l4; /* rest */
    uint64_t gm_l5; /* n */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_thf5kr0is9u9i gm_l8;
    {
        void * t7 = gm_l0;
        uint8_t t8 = (uint8_t)(((((uint8_t *)t7) + 24))[8] != 0);
        if (t8) {
            gm_tm98ap4112hee t9 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t7) + 24));
            gm_l2 = t9;
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t10;
        gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t10;
        void * t11 = (void *)((uint8_t *)&F.l3);
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_t4ce0jd3tdro4 t13;
        GM_ST(uint64_t, ((uint8_t *)&t13 + 0), t12);
        gm_tftb1odc739t5 t14 = gm_f_write_uqdv1oa(t11, t13);
        gm_l6 = t14;
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src4hj0beu8p6ao, 256, 5);
        void * t16 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t17 = gm_l1;
        gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
        gm_l7 = t18;
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src4hj0beu8p6ao, 257, 5);
        void * t20 = GM_LD(void *, ((uint8_t *)&F.l3));
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = (uintptr_t)t21;
        if (t22 > t23 || t23 > (uintptr_t)t21) gm_trap_slice(t22, t23, t21, gm_src4hj0beu8p6ao, 258, 13);
        uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
        gm_l4 = t25;
        {
            for (;;) {
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t27 = (t26 > ((uint64_t)0ULL));
                if (!t27) {
                    goto gm_lb2;
                }
                {
                    gm_tm98ap4112hee t28 = gm_l2;
                    gm_b16_8 t29 = gm_l4;
                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                    gm_node *pn31 = gm_find(cx, 2119230);
                    if (!pn31) gm_trap("no handler installed for effect `Proc`", gm_src4hj0beu8p6ao, 260, 14);
                    gm_thf5kr0is9u9i t32;
                    t32 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn31->clauses[1])(pn31->env, pn31->hctx, pn31, t28, t30);
                    if (gm_unw.kind) {
                        goto gm_ld6_1;
                    }
                    gm_l8 = t32;
                    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                    if (t33) {
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        gm_l5 = t34;
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                        goto gm_ld6_1;
                    }
                    uint64_t t35 = gm_l5;
                    uint8_t t36 = ((t35 == ((uint64_t)0ULL)));
                    if (t36) {
                        F.ret = GM_ZERO(gm_b0_1);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                        goto gm_ld6_1;
                    }
                    void * t37 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t39 = gm_l5;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = (uintptr_t)t38;
                    if (t40 > t41 || t41 > (uintptr_t)t38) gm_trap_slice(t40, t41, t38, gm_src4hj0beu8p6ao, 262, 16);
                    uint64_t t42 = ((uintptr_t)t38) - (uintptr_t)t39;
                    gm_b16_8 t43;
                    GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t37 + (uintptr_t)t39 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
                    gm_l4 = t43;
                }
gm_lb4:;
            }
        }
gm_lb2:;
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_r70pji5(t44);
    }
    goto gm_ls45;
gm_ld6_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t46 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_r70pji5(t46);
        gm_unw = gm_saved;
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
    return F.ret;
gm_fprop:
    return F.ret;
}

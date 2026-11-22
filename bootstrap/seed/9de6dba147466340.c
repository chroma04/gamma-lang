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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; } gm_t9rbkbpjtolar;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_t9rbkbpjtolar) == 6 && _Alignof(gm_t9rbkbpjtolar) == 1 && offsetof(gm_t9rbkbpjtolar, f0) == 0 && offsetof(gm_t9rbkbpjtolar, f1) == 1 && offsetof(gm_t9rbkbpjtolar, f2) == 2 && offsetof(gm_t9rbkbpjtolar, f3) == 3 && offsetof(gm_t9rbkbpjtolar, f4) == 4 && offsetof(gm_t9rbkbpjtolar, f5) == 5, "layout of `(Constraint, Constraint, Constraint, Constraint, Constraint, Constraint)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tnnjeerolos0a f0; gm_tnnjeerolos0a f1; } gm_tcf2f71ra2nps;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcf2f71ra2nps) == 16 && _Alignof(gm_tcf2f71ra2nps) == 4 && offsetof(gm_tcf2f71ra2nps, f0) == 0 && offsetof(gm_tcf2f71ra2nps, f1) == 8, "layout of `(ArrayKind, ArrayKind)`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_add_shared_fn_f8l99p1 {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    uint32_t ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_all_ord_8bea963 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_all_plain_6k5r6vb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_and_3tgam8m {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bits_struct_ty_l1kjtap {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_bound_to_5c11t0h {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_canonical_bytes_trb762l {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l21; /* x */
    gm_tpautr4br7h3l l29; /* def */
    gm_t0mglksrkfa5e l34; /* f */
    uint8_t ret;
};
struct gm_fr_canonical_field_otka8vm {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    uint8_t ret;
};
struct gm_fr_constraint_name_ombut3o {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_declare_constraint_s1juggf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
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
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0utaj4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_constraint_defer_52jt3m9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* deferred */
    gm_b0_1 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_lfqasgq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_954a9tt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_980ugfo {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9h7i0ht {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hfu2i92 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ii7qb9k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n0hp6t5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_6qnso5a {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_has_eq_iba53hs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    uint8_t ret;
};
struct gm_fr_has_eq_in_5a59avv {
    gm_frame h;
    gm_tpautr4br7h3l l16; /* def */
    gm_t0mglksrkfa5e l19; /* f */
    gm_tckdobunaulee l23; /* v */
    uint8_t ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_ord_1io9knm {
    gm_frame h;
    gm_tckdobunaulee l11; /* v */
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_implies_ibnta05 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inner_second_class_part_ufhro3f {
    gm_frame h;
    gm_toa2vhe3l4hfr l19; /* a */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_instantiate_35r5pb2 {
    gm_frame h;
    gm_tqqhoivahirr0 l5; /* sig */
    gm_trqfvmjtt4sij l6; /* map */
    gm_trqfvmjtt4sij l11; /* params */
    gm_t5v0c8ia6a5mo l14; /* p */
    gm_t5v0c8ia6a5mo l15; /* q */
    gm_trqfvmjtt4sij l16; /* kept */
    gm_tqqhoivahirr0 l17; /* inst */
    uint32_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_key_dmcraup {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* i */
    gm_trqfvmjtt4sij l4; /* bytes */
    gm_tbci5ospk5nnn l5; /* x */
    gm_t428ue2u3hf5g ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_n60iihm {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    uint8_t ret;
};
struct gm_fr_key_int_pqscuvn {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* v */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ib5k4gb {
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
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_match_effects_0l1vbuc {
    gm_frame h;
    gm_toa2vhe3l4hfr l16; /* x */
    gm_trqfvmjtt4sij l27; /* rest */
    gm_b0_1 ret;
};
struct gm_fr_match_fn_params_7to7qbv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_match_params_6vndkud {
    gm_frame h;
    gm_t53at80pfdus3 l5; /* a */
    gm_trqfvmjtt4sij l14; /* bytes */
    gm_toa2vhe3l4hfr l41; /* p */
    gm_tddtjfggdat0d l50; /* f */
    gm_tddtjfggdat0d l51; /* g */
    gm_tddtjfggdat0d l52; /* g */
    gm_tddtjfggdat0d l53; /* f */
    gm_tddtjfggdat0d l54; /* g */
    gm_b0_1 ret;
};
struct gm_fr_not_first_class_dve75oi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_param_constraints_2k5fnnl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_has_2r09oc7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parse_constraint_6f1aotv {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_plain_fields_i69c54j {
    gm_frame h;
    gm_tpautr4br7h3l l1; /* def */
    gm_t0mglksrkfa5e l4; /* f */
    uint8_t ret;
};
struct gm_fr_pop_knqt0be {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_1es0np6 {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_472i7vl {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_aqjeqnr_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hv483nm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_sp4si0f {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
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
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9a4nica {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_d2ob6vu {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hvlnmhb_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icepq2q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lm3950p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_satisfies_p0n2er8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_scope_type_params_2di4dm5 {
    gm_frame h;
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_t66j1im9ip128 ret;
};
struct gm_fr_second_class_part_k6i54mk {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_soa_const_view_od97rln {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_marker_heil9i9 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsatisfied_jnmtj8v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_value_at_sthggua {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_value_key_vki574i {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tbci5ospk5nnn l8; /* i */
    gm_trqfvmjtt4sij l10; /* bytes */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trqfvmjtt4sij l18; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmvsckgkj25(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_all_ord_8bea963(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_all_plain_6k5r6vb(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_canonical_bytes_trb762l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_canonical_field_otka8vm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_uarsnut(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_954a9tt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_980ugfo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9h7i0ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hfu2i92(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_ii7qb9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n0hp6t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_eq_in_5a59avv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_implies_ibnta05(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_5jc3e2i(gm_b16_8 a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_inner_second_class_part_ufhro3f(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_effects_0l1vbuc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_fn_params_7to7qbv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_constraint_6f1aotv(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j_c(void * a0, gm_tpautr4br7h3l a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1es0np6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aqjeqnr_2(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d2ob6vu(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvlnmhb_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep_c(void * a0, gm_t66j1im9ip128 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_value_at_sthggua(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tjoi8o798ne9k gm_f_value_at_sthggua_c(gm_tkaapcii86udk a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cem22767mo4[4] __attribute__((aligned(4)));
extern const uint8_t gm_s0rru461jb8nu[7];
extern const uint8_t gm_s3bddqij01brp[83];
extern const uint8_t gm_s670np414hdkp[8];
extern const uint8_t gm_s7kt2bei71pgo[4];
extern const uint8_t gm_s7s25m3nlrif2[6];
extern const uint8_t gm_s9jd8cvhje3jm[26];
extern const uint8_t gm_sd2s2op6i577j[36];
extern const uint8_t gm_sds8ed13tvvnt[48];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_seb2o268bfiia[11];
extern const uint8_t gm_seggohi50ng09[4];
extern const uint8_t gm_sfv26hkjngt1i[6];
extern const uint8_t gm_sh2bu6u8oapg5[73];
extern const uint8_t gm_sicfped8s368m[58];
extern const uint8_t gm_slf85brqp8fiu[125];
extern const uint8_t gm_snoj0t5lf2lht[4];
extern const uint8_t gm_sp525daee5j3r[85];
extern const uint8_t gm_sqri8tum3tpfm[3];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srio0gkdttkj2[61];
extern const uint8_t gm_srj9vrlard0qi[14];
extern const gm_tmpl gm_tpl4clb07vpsm45;
extern const gm_tmpl gm_tpl7da8g7lhjmr1;
extern const gm_tmpl gm_tpl85lm1gq83hlb;
extern const gm_tmpl gm_tplin004j5gtkru;
extern const gm_tmpl gm_tpltue438lnncbq;
extern const gm_tmpl gm_tpluuj281d42vhc;
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
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline bool gm_eqmvsckgkj25(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t5, t7);
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
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_fn_t4t83qh F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 344);
        gm_tftb1odc739t5 t4 = gm_f_push_472i7vl(t3, &(*(gm_tqqhoivahirr0 *)((uint8_t *)a1)));
        gm_l3 = t4;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmhupam4se3h5, 466, 5);
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 384);
        gm_tftb1odc739t5 t8 = gm_f_push_roqi806(t7, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        gm_l4 = t8;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmhupam4se3h5, 467, 5);
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 344) + 8));
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 468, 13);
    uint64_t t14 = r12;
    uint32_t t15 = (uint32_t)t14;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_add_shared_fn_f8l99p1 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l2;
    gm_tqqhoivahirr0 t4 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)a1));
    gm_tdgfduqjheqdm t5;
    gm_f_bodiless_def_u9kqo09((void *)&t5, t3);
    uint32_t t6 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t4, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 136));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 136) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_and_3tgam8m F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)0ULL));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t4 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 60, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 8), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 24), t4);
    GM_ST(void *, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 40), ((void *)0));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 48), t5);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 56), ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0utaj4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_lfqasgq F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_j4iu2lk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
inline __attribute__((aligned(4))) void gm_f_format_954a9tt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_954a9tt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9jd8cvhje3jm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpluuj281d42vhc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_980ugfo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_980ugfo F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sds8ed13tvvnt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl85lm1gq83hlb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9h7i0ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9h7i0ht F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3bddqij01brp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(82));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4clb07vpsm45), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hfu2i92(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_hfu2i92 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slf85brqp8fiu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(124));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplin004j5gtkru), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ii7qb9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ii7qb9k F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh2bu6u8oapg5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7da8g7lhjmr1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n0hp6t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n0hp6t5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp525daee5j3r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltue438lnncbq), t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_6qnso5a F;
    void * gm_l0; /* m */
    gm_tiilmfd16v3fc gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_uarsnut(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 40) + 32));
    gm_t9805qtonbsag t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[4] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_has_eq_iba53hs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij t2;
    gm_f_list_nof7vfj((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    void * t5 = (void *)((uint8_t *)&F.l2);
    uint8_t t6 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)2ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d3uqusp();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l3;
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l1), 40);
    }
    void * t2 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t2);
    uint8_t t3 = gm_l0;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l3 = t5;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    {
        uint8_t t6 = gm_l3;
        gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), t6);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t7);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_constraints_2k5fnnl F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1128));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1128) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srckg5f2c0p31ur, 252, 78);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + (uintptr_t)t6 * 48) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_kind_a0aikmb F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1128));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1128) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srckg5f2c0p31ur, 249, 62);
    gm_tnnjeerolos0a t7 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t3) + (uintptr_t)t6 * 48));
    F.ret = t7;
    goto gm_fret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_knqt0be F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* x */
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
        gm_tkn42u7na3t81 t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_t88rcucg1b7uk t27 = gm_l2;
    gm_tkn42u7na3t81 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1es0np6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1es0np6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_j0utaj4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_d2ob6vu(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_472i7vl F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ot5ttb7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_icepq2q(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 136), ((uint8_t *)a1), 136);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aqjeqnr_2(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_aqjeqnr_2 F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3jt8vgk_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hvlnmhb_2(t8, ((uint64_t)1ULL), t10);
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
    gm_t9qkh0rsooc8d t18 = gm_l1;
    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t16) + (uintptr_t)t17 * 1), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d1r4h19 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_92arfe8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_m244brh(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)&gm_l1), 24);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_he774nl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7sj41hq(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_roqi806 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1fu8oa9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9a4nica(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)a1), 64);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sp4si0f F;
    void * gm_l0; /* l */
    gm_tommf4j9upomc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d0lhrvs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lm3950p(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9a4nica F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d2ob6vu(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_d2ob6vu F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvlnmhb_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hvlnmhb_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_icepq2q F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lm3950p F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_m244brh F;
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t2, &t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_const_view_od97rln F;
    void * gm_l0; /* tys */
    uint32_t gm_l1; /* pat */
    uint32_t gm_l2; /* actual */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = gm_l1;
    uint32_t t5 = gm_f_template_9o92cvv(t3, t4);
    gm_b16_8 t6 = gm_f_adt_name_h7mjik7(t2, t5);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_srj9vrlard0qi));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(13));
    uint8_t t8 = (gm_eq0iqiubfm7e(((uint8_t *)&t6), ((uint8_t *)&t7)));
    if (t8) {
        void * t9 = gm_l0;
        void * t10 = gm_l0;
        uint32_t t11 = gm_l2;
        uint32_t t12 = gm_f_template_9o92cvv(t10, t11);
        gm_b16_8 t13 = gm_f_adt_name_h7mjik7(t9, t12);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s670np414hdkp));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        gm_l3 = t15;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t16 = gm_l3;
    F.ret = t16;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t6, t8);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_template_marker_heil9i9 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_seb2o268bfiia));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_t428ue2u3hf5g t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)21ULL));
    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t3, &t6);
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
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unscope_type_params_bofhhep F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_f_restore_scope_edqq1v8(t2, &(*(gm_t66j1im9ip128 *)((uint8_t *)a1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t2);
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
extern __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_all_ord_8bea963(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_ord_8bea963 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* elems */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l4; /* e */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l1;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + (uintptr_t)t13 * 4));
                        gm_l4 = t14;
                        void * t15 = gm_l0;
                        gm_t88rcucg1b7uk t16 = gm_l4;
                        uint8_t t17 = gm_f_has_ord_1io9knm(t15, GM_CONV(gm_csguankko4lmhd, t16));
                        uint8_t t18 = (uint8_t)!t17;
                        if (t18) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 99, 5);
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
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_all_plain_6k5r6vb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_plain_6k5r6vb F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* elems */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* e */
    GM_STACK_CHECK(&F, 308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlcm4lvu3g1a8 gm_l6;
    uint8_t gm_l7;
    gm_tf5srh4mp4b3i gm_l8;
    gm_b16_8 t10 = gm_l1;
    gm_l2 = t10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l3;
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + (uintptr_t)t17 * 4));
                gm_tlcm4lvu3g1a8 t19;
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19 + 8), t18);
                gm_l6 = t19;
                {
                    {
                        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        gm_l4 = t20;
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
                        gm_l5 = t21;
                        void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t24 = gm_l4;
                        uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t26 = (uintptr_t)t24;
                        if (t25 > t26 || t26 > (uintptr_t)t23) gm_trap_slice(t25, t26, t23, gm_srckg5f2c0p31ur, 167, 12);
                        uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t28;
                        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 4)));
                        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
                        gm_t88rcucg1b7uk t29 = gm_l5;
                        gm_tf5srh4mp4b3i t30 = gm_f_index_of_5jc3e2i(t28, GM_CONV(gm_csguankko4lmhd, t29));
                        gm_l8 = t30;
                        {
                            {
                                uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                if (t31) {
                                    goto gm_lb7;
                                }
                                gm_l7 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l7 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 167, 12);
                        }
gm_lb6:;
                        uint8_t t32 = gm_l7;
                        if (t32) {
                            goto gm_lb3;
                        }
                        void * t33 = gm_l0;
                        gm_t88rcucg1b7uk t34 = gm_l5;
                        uint8_t t35 = gm_f_is_plain_n60iihm(t33, GM_CONV(gm_csguankko4lmhd, t34));
                        uint8_t t36 = (uint8_t)!t35;
                        if (t36) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 164, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l3;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l3 = t40;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_struct_ty_l1kjtap F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)17ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t11;
            {
                {
                    void * t12 = gm_l0;
                    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1088));
                    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1088) + 8));
                    uint32_t t15 = gm_l2;
                    uint64_t t16 = (uint64_t)t15;
                    if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srckg5f2c0p31ur, 189, 21);
                    uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 8), 48);
                    gm_tkn42u7na3t81 t19 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l3) + 40));
                    F.ret = t19;
                    goto gm_fret;
                }
gm_lb4:;
                gm_tkn42u7na3t81 t20;
                memset(&t20, 0, sizeof t20);
                F.ret = t20;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t21;
        memset(&t21, 0, sizeof t21);
        F.ret = t21;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_to_5c11t0h F;
    gm_b16_8 gm_l0; /* binds */
    uint32_t gm_l1; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l4; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        gm_tplpbqf3duvn3 t14 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t11) + (uintptr_t)t13 * 8));
                        gm_l4 = t14;
                        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t16 = gm_l1;
                        uint8_t t17 = ((t15 == t16));
                        if (t17) {
                            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 4));
                            gm_tkn42u7na3t81 t19;
                            GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19), t18);
                            ((uint8_t *)&t19)[4] = 1;
                            F.ret = t19;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 336, 5);
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
    gm_tkn42u7na3t81 t24;
    memset(&t24, 0, sizeof t24);
    F.ret = t24;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_canonical_bytes_trb762l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_canonical_bytes_trb762l F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l3; /* out */
    void * gm_l4; /* tys */
    unsigned __int128 gm_l5; /* bits */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* b */
    double gm_l10; /* x */
    uint64_t gm_l11; /* raw */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l15; /* a */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* size */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l22; /* elems */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t88rcucg1b7uk gm_l27; /* e */
    uint32_t gm_l28; /* id */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    GM_STACK_CHECK(&F, 2154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l35;
    gm_t53at80pfdus3 gm_l36;
    gm_t53at80pfdus3 gm_l37;
    gm_tbccvikg6b96e gm_l38;
    gm_thf8as07kj3uj gm_l39;
    uint8_t gm_l40;
    gm_tbccvikg6b96e gm_l41;
    gm_tlcm4lvu3g1a8 gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_tbccvikg6b96e gm_l45;
    gm_t8ta482bl6be0 gm_l46;
    uint8_t gm_l47;
    {
        void * t51 = gm_l0;
        void * t52 = (void *)(((uint8_t *)t51) + 8);
        gm_l4 = t52;
        {
            {
                {
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)0ULL));
                    if (!t54) {
                        goto gm_lb2;
                    }
                    unsigned __int128 t55 = GM_LD(unsigned __int128, (((uint8_t *)a1) + 16));
                    gm_l5 = t55;
                    gm_l7 = ((uint64_t)0ULL);
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    gm_l6 = t56;
                    {
                        for (;;) {
                            uint64_t t57 = gm_l7;
                            uint64_t t58 = gm_l6;
                            uint8_t t59 = (t57 < t58);
                            if (!t59) {
                                goto gm_lb3;
                            }
                            {
                                {
                                    {
                                        uint64_t t60 = gm_l7;
                                        gm_l8 = t60;
                                        void * t61 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t63 = gm_l8;
                                        unsigned __int128 t64 = gm_l5;
                                        uint64_t t65 = gm_l8;
                                        uint64_t r66; bool o67 = __builtin_mul_overflow(t65, ((uint64_t)8ULL), &r66);
                                        if (o67) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 534, 58);
                                        uint64_t t68 = r66;
                                        unsigned __int128 t69 = (unsigned __int128)t68;
                                        unsigned __int128 n70 = (unsigned __int128)t69;
                                        if (n70 >= 128) gm_trap_shift((uint64_t)n70, "u128", gm_srckg5f2c0p31ur, 534, 45);
                                        unsigned __int128 t71 = (unsigned __int128)(t64 >> (int)n70);
                                        uint8_t t72 = (uint8_t)t71;
                                        GM_ST(uint8_t, (((uint8_t *)t61) + (uintptr_t)t63 * 1), t72);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 534, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t73 = gm_l7;
                            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                            uint64_t t76 = r74;
                            gm_l7 = t76;
                        }
                    }
gm_lb3:;
                    void * t77 = gm_l4;
                    gm_t88rcucg1b7uk t78 = gm_l2;
                    uint8_t t79 = gm_f_is_integer_8d4f9ek(t77, GM_CONV(gm_csguankko4lmhd, t78));
                    F.ret = t79;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)2ULL));
                    if (!t81) {
                        goto gm_lb8;
                    }
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)a1) + 16));
                    gm_l9 = t82;
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t84 = (t83 > ((uint64_t)0ULL));
                    if (t84) {
                        void * t85 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint8_t t87 = gm_l9;
                        if (t87) {
                            gm_l35 = ((uint8_t)1ULL);
                        } else {
                            gm_l35 = ((uint8_t)0ULL);
                        }
                        uint8_t t88 = gm_l35;
                        GM_ST(uint8_t, (((uint8_t *)t85) + (uintptr_t)((uint64_t)0ULL) * 1), t88);
                    }
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb8:;
                {
                    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t90 = (uint8_t)(t89 == ((uint8_t)1ULL));
                    if (!t90) {
                        goto gm_lb9;
                    }
                    double t91 = GM_LD(double, (((uint8_t *)a1) + 16));
                    gm_l10 = t91;
                    gm_l11 = ((uint64_t)0ULL);
                    void * t92 = gm_l4;
                    gm_t88rcucg1b7uk t93 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l36, t92, GM_CONV(gm_csguankko4lmhd, t93));
                    {
                        {
                            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)3ULL));
                            if (!t95) {
                                goto gm_lb11;
                            }
                            uint16_t t96 = GM_LD(uint16_t, (((uint8_t *)&gm_l36) + 8));
                            uint8_t t97 = ((t96 == ((uint16_t)64ULL)));
                            if (!t97) {
                                goto gm_lb11;
                            }
                            double t98 = gm_l10;
                            uint64_t t99;
                            memset(&t99, 0, sizeof t99); memcpy(&t99, ((uint8_t *)&t98), sizeof t99 < 8 ? sizeof t99 : 8);
                            gm_l11 = t99;
                            {
                                {
                                    uint64_t t100 = gm_l11;
                                    uint64_t t101 = (uint64_t)(t100 & ((uint64_t)4503599627370495ULL));
                                    uint8_t t102 = (!(t101 == ((uint64_t)0ULL)));
                                    if (!t102) {
                                        goto gm_lb13;
                                    }
                                    uint64_t t103 = gm_l11;
                                    unsigned __int128 n104 = (unsigned __int128)((uint64_t)52ULL);
                                    if (n104 >= 64) gm_trap_shift((uint64_t)n104, "u64", gm_srckg5f2c0p31ur, 546, 60);
                                    uint64_t t105 = (uint64_t)(t103 >> (int)n104);
                                    uint64_t t106 = (uint64_t)(t105 & ((uint64_t)2047ULL));
                                    uint8_t t107 = ((t106 == ((uint64_t)2047ULL)));
                                    if (!t107) {
                                        goto gm_lb13;
                                    }
                                    gm_l11 = ((uint64_t)9221120237041090560ULL);
                                    goto gm_lb12;
                                }
gm_lb13:;
                            }
gm_lb12:;
                            uint64_t t108 = gm_l11;
                            uint64_t t109 = (uint64_t)(t108 & ((uint64_t)9223372036854775807ULL));
                            uint8_t t110 = ((t109 == ((uint64_t)0ULL)));
                            if (t110) {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t112 = (uint8_t)(t111 == ((uint8_t)3ULL));
                            if (!t112) {
                                goto gm_lb14;
                            }
                            uint16_t t113 = GM_LD(uint16_t, (((uint8_t *)&gm_l36) + 8));
                            uint8_t t114 = ((t113 == ((uint16_t)32ULL)));
                            if (!t114) {
                                goto gm_lb14;
                            }
                            double t115 = gm_l10;
                            float t116 = (float)((double)t115);
                            uint32_t t117;
                            memset(&t117, 0, sizeof t117); memcpy(&t117, ((uint8_t *)&t116), sizeof t117 < 4 ? sizeof t117 : 4);
                            uint64_t t118 = (uint64_t)t117;
                            gm_l11 = t118;
                            {
                                {
                                    uint64_t t119 = gm_l11;
                                    uint64_t t120 = (uint64_t)(t119 & ((uint64_t)8388607ULL));
                                    uint8_t t121 = (!(t120 == ((uint64_t)0ULL)));
                                    if (!t121) {
                                        goto gm_lb16;
                                    }
                                    uint64_t t122 = gm_l11;
                                    unsigned __int128 n123 = (unsigned __int128)((uint64_t)23ULL);
                                    if (n123 >= 64) gm_trap_shift((uint64_t)n123, "u64", gm_srckg5f2c0p31ur, 551, 53);
                                    uint64_t t124 = (uint64_t)(t122 >> (int)n123);
                                    uint64_t t125 = (uint64_t)(t124 & ((uint64_t)255ULL));
                                    uint8_t t126 = ((t125 == ((uint64_t)255ULL)));
                                    if (!t126) {
                                        goto gm_lb16;
                                    }
                                    gm_l11 = ((uint64_t)2143289344ULL);
                                    goto gm_lb15;
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t127 = gm_l11;
                            uint64_t t128 = (uint64_t)(t127 & ((uint64_t)2147483647ULL));
                            uint8_t t129 = ((t128 == ((uint64_t)0ULL)));
                            if (t129) {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 543, 13);
                    }
gm_lb10:;
                    gm_l13 = ((uint64_t)0ULL);
                    uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    gm_l12 = t130;
                    {
                        for (;;) {
                            uint64_t t131 = gm_l13;
                            uint64_t t132 = gm_l12;
                            uint8_t t133 = (t131 < t132);
                            if (!t133) {
                                goto gm_lb18;
                            }
                            {
                                {
                                    {
                                        uint64_t t134 = gm_l13;
                                        gm_l14 = t134;
                                        void * t135 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t137 = gm_l14;
                                        uint64_t t138 = gm_l11;
                                        uint64_t t139 = gm_l14;
                                        uint64_t r140; bool o141 = __builtin_mul_overflow(t139, ((uint64_t)8ULL), &r140);
                                        if (o141) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 556, 56);
                                        uint64_t t142 = r140;
                                        uint64_t t143 = (uint64_t)t142;
                                        unsigned __int128 n144 = (unsigned __int128)t143;
                                        if (n144 >= 64) gm_trap_shift((uint64_t)n144, "u64", gm_srckg5f2c0p31ur, 556, 45);
                                        uint64_t t145 = (uint64_t)(t138 >> (int)n144);
                                        uint8_t t146 = (uint8_t)t145;
                                        GM_ST(uint8_t, (((uint8_t *)t135) + (uintptr_t)t137 * 1), t146);
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 556, 13);
                                }
gm_lb21:;
                            }
gm_lb20:;
                            uint64_t t147 = gm_l13;
                            uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                            uint64_t t150 = r148;
                            gm_l13 = t150;
                        }
                    }
gm_lb18:;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb9:;
            }
            {
                {
                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)3ULL));
                    if (!t152) {
                        goto gm_lb23;
                    }
                    void * t153 = GM_LD(void *, (((uint8_t *)a1) + 16));
                    gm_l15 = t153;
                    void * t154 = gm_l4;
                    gm_t88rcucg1b7uk t155 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l37, t154, GM_CONV(gm_csguankko4lmhd, t155));
                    {
                        {
                            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t157 = (uint8_t)(t156 == ((uint8_t)11ULL));
                            if (!t157) {
                                goto gm_lb25;
                            }
                            gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 8));
                            gm_l16 = t158;
                            void * t159 = gm_l0;
                            gm_t88rcucg1b7uk t160 = gm_l16;
                            gm_f_layout_a5ovnrm((void *)&gm_l38, (void *)NULL, cx, NULL, t159, t160);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t161 = GM_LD(uint64_t, ((uint8_t *)&gm_l38));
                            uint64_t t162 = (uint64_t)t161;
                            gm_l17 = t162;
                            void * t163 = gm_l15;
                            gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)t163));
                            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                            gm_l18 = t165;
                            gm_l19 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t166 = gm_l19;
                                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint8_t t168 = (t166 < t167);
                                    if (!t168) {
                                        goto gm_lb26;
                                    }
                                    {
                                        {
                                            uint64_t t169 = gm_l19;
                                            void * t170 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                            uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                            uint64_t t172 = gm_l19;
                                            gm_t6evimt84lndq t173 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t170) + (uintptr_t)t172 * 48));
                                            GM_ST(uint64_t, (uint8_t *)&gm_l39 + 8, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l39 + 0), t169);
                                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&gm_l39 + 16), t173);
                                        }
                                        {
                                            {
                                                uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)&gm_l39));
                                                gm_l20 = t174;
                                                memmove(((uint8_t *)&F.l21), (((uint8_t *)&gm_l39) + 16), 48);
                                                uint64_t t175 = gm_l20;
                                                uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                                                if (o177) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 563, 25);
                                                uint64_t t178 = r176;
                                                uint64_t t179 = gm_l17;
                                                uint64_t r180; bool o181 = __builtin_mul_overflow(t178, t179, &r180);
                                                if (o181) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 563, 24);
                                                uint64_t t182 = r180;
                                                uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                uint8_t t184 = (t182 > t183);
                                                if (t184) {
                                                    gm_l40 = ((uint8_t)1);
                                                } else {
                                                    void * t185 = gm_l0;
                                                    gm_t88rcucg1b7uk t186 = gm_l16;
                                                    void * t187 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                    uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                    uint64_t t189 = gm_l20;
                                                    uint64_t t190 = gm_l17;
                                                    uint64_t r191; bool o192 = __builtin_mul_overflow(t189, t190, &r191);
                                                    if (o192) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 563, 87);
                                                    uint64_t t193 = r191;
                                                    uint64_t t194 = gm_l20;
                                                    uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                                                    if (o196) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 563, 98);
                                                    uint64_t t197 = r195;
                                                    uint64_t t198 = gm_l17;
                                                    uint64_t r199; bool o200 = __builtin_mul_overflow(t197, t198, &r199);
                                                    if (o200) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 563, 97);
                                                    uint64_t t201 = r199;
                                                    uint64_t t202 = (uintptr_t)t193;
                                                    uint64_t t203 = (uintptr_t)t201;
                                                    if (t202 > t203 || t203 > (uintptr_t)t188) gm_trap_slice(t202, t203, t188, gm_srckg5f2c0p31ur, 563, 83);
                                                    uint64_t t204 = ((uintptr_t)t201) - (uintptr_t)t193;
                                                    gm_b16_8 t205;
                                                    GM_ST(void *, (uint8_t *)&t205, (void *)(((uint8_t *)t187 + (uintptr_t)t193 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(t204));
                                                    uint8_t t206 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t185, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l21)), t186, t205);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t207 = (uint8_t)!t206;
                                                    gm_l40 = t207;
                                                }
                                                uint8_t t208 = gm_l40;
                                                if (t208) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb29;
                                            }
gm_lb30:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 562, 17);
                                        }
gm_lb29:;
                                    }
gm_lb28:;
                                    uint64_t t209 = gm_l19;
                                    uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                                    uint64_t t212 = r210;
                                    gm_l19 = t212;
                                }
                            }
gm_lb26:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb25:;
                        {
                            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t214 = (uint8_t)(t213 == ((uint8_t)12ULL));
                            if (!t214) {
                                goto gm_lb31;
                            }
                            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                            gm_l22 = t215;
                            void * t216 = gm_l0;
                            gm_t88rcucg1b7uk t217 = gm_l2;
                            gm_f_layout_a5ovnrm((void *)&gm_l41, (void *)NULL, cx, NULL, t216, t217);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 16));
                            gm_l23 = t218;
                            gm_b16_8 t219 = gm_l22;
                            gm_l24 = t219;
                            gm_l25 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t220 = gm_l25;
                                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                    uint8_t t222 = (t220 < t221);
                                    if (!t222) {
                                        goto gm_lb32;
                                    }
                                    {
                                        uint64_t t223 = gm_l25;
                                        void * t224 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                        uint64_t t226 = gm_l25;
                                        gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t224) + (uintptr_t)t226 * 4));
                                        gm_tlcm4lvu3g1a8 t228;
                                        GM_ST(uint32_t, (uint8_t *)&t228 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t228 + 0), t223);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t228 + 8), t227);
                                        gm_l42 = t228;
                                        {
                                            {
                                                uint64_t t229 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                                gm_l26 = t229;
                                                gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 8));
                                                gm_l27 = t230;
                                                uint64_t t231 = gm_l26;
                                                void * t232 = gm_l15;
                                                uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)t232) + 8));
                                                uint8_t t234 = (t231 >= t233);
                                                if (t234) {
                                                    gm_l43 = ((uint8_t)1);
                                                } else {
                                                    void * t235 = gm_l0;
                                                    void * t236 = gm_l15;
                                                    void * t237 = GM_LD(void *, ((uint8_t *)t236));
                                                    uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)t236) + 8));
                                                    uint64_t t239 = gm_l26;
                                                    if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_srckg5f2c0p31ur, 570, 66);
                                                    gm_t88rcucg1b7uk t240 = gm_l27;
                                                    void * t241 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    uint64_t t243 = gm_l26;
                                                    if ((uintptr_t)t243 >= (uintptr_t)(t242)) gm_trap_index((uintptr_t)t243, t242, gm_srckg5f2c0p31ur, 570, 81);
                                                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)t241) + (uintptr_t)t243 * 8));
                                                    gm_b16_8 t245 = gm_l3;
                                                    uint8_t t246 = gm_f_canonical_field_otka8vm((void *)NULL, cx, NULL, t235, &(*(gm_t6evimt84lndq *)(((uint8_t *)t237) + (uintptr_t)t239 * 48)), t240, t244, t245);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t247 = (uint8_t)!t246;
                                                    gm_l43 = t247;
                                                }
                                                uint8_t t248 = gm_l43;
                                                if (t248) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb35;
                                            }
gm_lb36:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 569, 17);
                                        }
gm_lb35:;
                                    }
gm_lb34:;
                                    uint64_t t249 = gm_l25;
                                    uint64_t r250; bool o251 = __builtin_add_overflow(t249, ((uint64_t)1ULL), &r250);
                                    uint64_t t252 = r250;
                                    gm_l25 = t252;
                                }
                            }
gm_lb32:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb31:;
                        {
                            uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t254 = (uint8_t)(t253 == ((uint8_t)17ULL));
                            if (!t254) {
                                goto gm_lb37;
                            }
                            uint32_t t255 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 8));
                            gm_l28 = t255;
                            {
                                {
                                    {
                                        void * t256 = gm_l0;
                                        void * t257 = GM_LD(void *, (((uint8_t *)t256) + 1088));
                                        uint64_t t258 = GM_LD(uint64_t, ((((uint8_t *)t256) + 1088) + 8));
                                        uint32_t t259 = gm_l28;
                                        uint64_t t260 = (uint64_t)t259;
                                        if ((uintptr_t)t260 >= (uintptr_t)(t258)) gm_trap_index((uintptr_t)t260, t258, gm_srckg5f2c0p31ur, 575, 33);
                                        uint8_t t261 = GM_LD(uint8_t, (((((uint8_t *)t257) + (uintptr_t)t260 * 184) + 24) + 0));
                                        uint8_t t262 = (uint8_t)(t261 == ((uint8_t)0ULL));
                                        if (!t262) {
                                            goto gm_lb39;
                                        }
                                        memmove(((uint8_t *)&F.l29), (((((uint8_t *)t257) + (uintptr_t)t260 * 184) + 24) + 8), 48);
                                    }
                                    {
                                        {
                                            uint8_t t263 = (uint8_t)(((((uint8_t *)&F.l29) + 40))[4] != 0);
                                            if (t263) {
                                                goto gm_lb41;
                                            }
                                            gm_l44 = ((uint8_t)0);
                                            goto gm_lb40;
                                        }
gm_lb41:;
                                        {
                                            gm_l44 = ((uint8_t)1);
                                            goto gm_lb40;
                                        }
gm_lb42:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 576, 20);
                                    }
gm_lb40:;
                                    uint8_t t264 = gm_l44;
                                    if (t264) {
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    {
                                        void * t265 = gm_l0;
                                        gm_t88rcucg1b7uk t266 = gm_l2;
                                        gm_f_layout_a5ovnrm((void *)&gm_l45, (void *)NULL, cx, NULL, t265, t266);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 16));
                                    gm_l30 = t267;
                                    gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                    gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                                    gm_l31 = t269;
                                    gm_l32 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t270 = gm_l32;
                                            uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                            uint8_t t272 = (t270 < t271);
                                            if (!t272) {
                                                goto gm_lb43;
                                            }
                                            {
                                                {
                                                    uint64_t t273 = gm_l32;
                                                    void * t274 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                    uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                    uint64_t t276 = gm_l32;
                                                    gm_t0mglksrkfa5e t277 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t274) + (uintptr_t)t276 * 40));
                                                    GM_ST(uint64_t, ((uint8_t *)&gm_l46 + 0), t273);
                                                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l46 + 8), t277);
                                                }
                                                {
                                                    {
                                                        uint64_t t278 = GM_LD(uint64_t, ((uint8_t *)&gm_l46));
                                                        gm_l33 = t278;
                                                        memmove(((uint8_t *)&F.l34), (((uint8_t *)&gm_l46) + 8), 40);
                                                        uint64_t t279 = gm_l33;
                                                        void * t280 = gm_l15;
                                                        uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)t280) + 8));
                                                        uint8_t t282 = (t279 >= t281);
                                                        if (t282) {
                                                            gm_l47 = ((uint8_t)1);
                                                        } else {
                                                            void * t283 = gm_l0;
                                                            void * t284 = gm_l15;
                                                            void * t285 = GM_LD(void *, ((uint8_t *)t284));
                                                            uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)t284) + 8));
                                                            uint64_t t287 = gm_l33;
                                                            if ((uintptr_t)t287 >= (uintptr_t)(t286)) gm_trap_index((uintptr_t)t287, t286, gm_srckg5f2c0p31ur, 579, 66);
                                                            gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 16));
                                                            void * t289 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                                            uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                                            uint64_t t291 = gm_l33;
                                                            if ((uintptr_t)t291 >= (uintptr_t)(t290)) gm_trap_index((uintptr_t)t291, t290, gm_srckg5f2c0p31ur, 579, 84);
                                                            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)t289) + (uintptr_t)t291 * 8));
                                                            gm_b16_8 t293 = gm_l3;
                                                            uint8_t t294 = gm_f_canonical_field_otka8vm((void *)NULL, cx, NULL, t283, &(*(gm_t6evimt84lndq *)(((uint8_t *)t285) + (uintptr_t)t287 * 48)), t288, t292, t293);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t295 = (uint8_t)!t294;
                                                            gm_l47 = t295;
                                                        }
                                                        uint8_t t296 = gm_l47;
                                                        if (t296) {
                                                            F.ret = ((uint8_t)0);
                                                            goto gm_fret;
                                                        }
                                                        goto gm_lb46;
                                                    }
gm_lb47:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 578, 17);
                                                }
gm_lb46:;
                                            }
gm_lb45:;
                                            uint64_t t297 = gm_l32;
                                            uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                                            uint64_t t300 = r298;
                                            gm_l32 = t300;
                                        }
                                    }
gm_lb43:;
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
gm_lb39:;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
gm_lb38:;
                        }
gm_lb37:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb48:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 559, 20);
                    }
gm_lb24:;
                }
gm_lb23:;
            }
            {
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb49:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 532, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_canonical_field_otka8vm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4) {
    struct gm_fr_canonical_field_otka8vm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint64_t gm_l3; /* offset */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* end */
    GM_STACK_CHECK(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbccvikg6b96e gm_l7;
    uint64_t t2 = gm_l3;
    uint64_t t3 = (uint64_t)t2;
    gm_l5 = t3;
    {
        uint64_t t4 = gm_l5;
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_f_layout_a5ovnrm((void *)&gm_l7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        uint64_t t8 = (uint64_t)t7;
        uint64_t r9; bool o10 = __builtin_add_overflow(t4, t8, &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 592, 12);
        uint64_t t11 = r9;
        gm_l6 = t11;
    }
    uint64_t t12 = gm_l6;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t14 = (t12 > t13);
    if (t14) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l2;
        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t19 = gm_l5;
        uint64_t t20 = gm_l6;
        uint64_t t21 = (uintptr_t)t19;
        uint64_t t22 = (uintptr_t)t20;
        if (t21 > t22 || t22 > (uintptr_t)t18) gm_trap_slice(t21, t22, t18, gm_srckg5f2c0p31ur, 594, 37);
        uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)t19;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t17 + (uintptr_t)t19 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
        uint8_t t25 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t15, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t16, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t25;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_constraint_name_ombut3o F;
    gm_t9qkh0rsooc8d gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sqri8tum3tpfm));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s7kt2bei71pgo));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
            if (!t17) {
                goto gm_lb4;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snoj0t5lf2lht));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)3ULL));
            if (!t20) {
                goto gm_lb5;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_seggohi50ng09));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
            gm_l1 = t21;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)4ULL));
            if (!t23) {
                goto gm_lb6;
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_se19nf1421e37));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            gm_l1 = t24;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)5ULL));
            if (!t26) {
                goto gm_lb7;
            }
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s7s25m3nlrif2));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(5));
            gm_l1 = t27;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)6ULL));
            if (!t29) {
                goto gm_lb8;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sfv26hkjngt1i));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
            gm_l1 = t30;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 14, 55);
    }
gm_lb1:;
    gm_b16_8 t31 = gm_l1;
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_constraint_s1juggf F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* c */
    gm_b16_8 gm_l3; /* msg */
    gm_t9qkh0rsooc8d gm_l4; /* k */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_taari1s0m51a7 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_t53at80pfdus3 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t6 = gm_l2;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t6));
    gm_taari1s0m51a7 t8 = gm_f_parse_constraint_6f1aotv(t7);
    gm_l7 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
    if (t9) {
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l7));
        gm_l4 = t10;
    } else {
        void * t11 = gm_l2;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
        gm_tnpifv20irnph t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
        gm_f_format_n0hp6t5((void *)&gm_l8, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srckg5f2c0p31ur, 29, 16);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_l3 = t15;
        void * t16 = gm_l2;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)t16));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s0rru461jb8nu));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
        uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
        if (t19) {
            gm_b16_8 t20 = gm_l3;
            gm_tnpifv20irnph t21;
            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
            gm_f_format_954a9tt((void *)&gm_l9, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srckg5f2c0p31ur, 30, 44);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_l3 = t23;
        }
        void * t24 = gm_l0;
        void * t25 = gm_l2;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 16));
        gm_b16_8 t27 = gm_l3;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b0_1 t29 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t24, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t30 = gm_l4;
    gm_l4 = t30;
    void * t31 = gm_l0;
    uint64_t t32 = gm_l1;
    void * t33 = gm_l2;
    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 32));
    gm_t88rcucg1b7uk t35 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t31, t32, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t35;
    void * t36 = gm_l0;
    void * t37 = (void *)(((uint8_t *)t36) + 8);
    gm_t88rcucg1b7uk t38 = gm_l5;
    gm_f_kind_jcnt4hm((void *)&gm_l10, t37, GM_CONV(gm_csguankko4lmhd, t38));
    {
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)19ULL));
            if (!t40) {
                goto gm_lb2;
            }
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l6 = t41;
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 1128));
            uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 1128) + 8));
            uint32_t t45 = gm_l6;
            uint64_t t46 = (uint64_t)t45;
            if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_srckg5f2c0p31ur, 36, 22);
            void * t47 = (void *)((((uint8_t *)t43) + (uintptr_t)t46 * 48) + 8);
            gm_t9qkh0rsooc8d t48 = gm_l4;
            gm_tftb1odc739t5 t49 = gm_f_push_aqjeqnr_2(t47, t48);
            gm_l11 = t49;
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srckg5f2c0p31ur, 36, 22);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)26ULL));
            if (!t52) {
                goto gm_lb3;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            void * t53 = gm_l0;
            void * t54 = gm_l2;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 32));
            gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_srio0gkdttkj2));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(60));
            gm_b0_1 t58 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t53, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 35, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_constraint_defer_52jt3m9 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t8er5olt969ip gm_l4; /* d */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t7 = gm_l0;
    void * t8 = gm_l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 1888));
    uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 53, 5);
    uint32_t t12 = r10;
    GM_ST(uint32_t, (((uint8_t *)t7) + 1888), t12);
    void * t13 = gm_l0;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 1888));
    uint8_t t15 = (t14 > ((uint32_t)0ULL));
    if (t15) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        void * t16 = gm_l0;
        memmove(((uint8_t *)&F.l1), (((uint8_t *)t16) + 1896), 40);
    }
    {
        void * t17 = gm_l0;
        gm_trqfvmjtt4sij t18;
        gm_f_list_ib5k4gb((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t17) + 1896), t18);
    }
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_l2 = t19;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l3;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t25 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t23) + (uintptr_t)t25 * 28), 28);
                        void * t26 = gm_l0;
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
                        gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l4) + 4));
                        uint8_t t29 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t26, t27, t28);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t30 = (uint8_t)!t29;
                        if (t30) {
                            void * t31 = gm_l0;
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                            void * t33 = gm_l0;
                            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
                            gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l4) + 4));
                            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                            uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 12));
                            gm_b16_8 t38 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t33, t34, t35, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b0_1 t39 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t31, t32, t38);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 57, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l3;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l3 = t43;
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
extern __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_eq_in_5a59avv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_has_eq_in_5a59avv F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* seen */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* elems */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint32_t gm_l14; /* id */
    uint8_t gm_l15; /* r */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l20; /* variants */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_t88rcucg1b7uk gm_l24; /* p */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l25;
    uint8_t gm_l26;
    gm_tftb1odc739t5 gm_l27;
    void * t41 = gm_l0;
    void * t42 = (void *)(((uint8_t *)t41) + 8);
    gm_t88rcucg1b7uk t43 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l25, t42, GM_CONV(gm_csguankko4lmhd, t43));
    {
        {
            {
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)15ULL));
                    if (!t45) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)25ULL));
                    if (!t47) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)18ULL));
                    if (!t49) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)6ULL));
                    if (!t51) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)19ULL));
            if (!t53) {
                goto gm_lb8;
            }
            uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
            gm_l3 = t54;
            void * t55 = gm_l0;
            uint32_t t56 = gm_l3;
            gm_t9qkh0rsooc8d t57;
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
            uint8_t t58 = gm_f_param_has_2r09oc7(t55, t56, t57);
            F.ret = t58;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)12ULL));
            if (!t60) {
                goto gm_lb9;
            }
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
            gm_l4 = t61;
            gm_b16_8 t62 = gm_l4;
            gm_l5 = t62;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t63 = gm_l6;
                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t65 = (t63 < t64);
                    if (!t65) {
                        goto gm_lb10;
                    }
                    {
                        void * t66 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t68 = gm_l6;
                        {
                            {
                                gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + (uintptr_t)t68 * 4));
                                gm_l7 = t69;
                                void * t70 = gm_l0;
                                gm_t88rcucg1b7uk t71 = gm_l7;
                                void * t72 = gm_l2;
                                uint8_t t73 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t70, t71, t72);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t74 = (uint8_t)!t73;
                                if (t74) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 118, 13);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t75 = gm_l6;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l6 = t78;
                }
            }
gm_lb10:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb9:;
        {
            {
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)11ULL));
                    if (!t80) {
                        goto gm_lb17;
                    }
                    gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 8));
                    gm_l8 = t81;
                    goto gm_lb16;
                }
gm_lb17:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)20ULL));
                    if (!t83) {
                        goto gm_lb18;
                    }
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
                    gm_l8 = t84;
                    goto gm_lb16;
                }
gm_lb18:;
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)10ULL));
                    if (!t86) {
                        goto gm_lb19;
                    }
                    gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
                    gm_l8 = t87;
                    goto gm_lb16;
                }
gm_lb19:;
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)13ULL));
                    if (!t89) {
                        goto gm_lb20;
                    }
                    gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 8));
                    gm_l8 = t90;
                    goto gm_lb16;
                }
gm_lb20:;
                goto gm_lb15;
            }
gm_lb16:;
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l8;
            void * t93 = gm_l2;
            uint8_t t94 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t91, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t94;
            goto gm_fret;
        }
gm_lb15:;
        {
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)14ULL));
            if (!t96) {
                goto gm_lb21;
            }
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 8));
            gm_l12 = t97;
            gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l13 = t98;
            void * t99 = gm_l0;
            gm_t88rcucg1b7uk t100 = gm_l12;
            void * t101 = gm_l2;
            uint8_t t102 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t99, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t102) {
                void * t103 = gm_l0;
                gm_t88rcucg1b7uk t104 = gm_l13;
                void * t105 = gm_l2;
                uint8_t t106 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t103, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t106;
            } else {
                gm_l26 = ((uint8_t)0);
            }
            uint8_t t107 = gm_l26;
            F.ret = t107;
            goto gm_fret;
        }
gm_lb21:;
        {
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)17ULL));
            if (!t109) {
                goto gm_lb22;
            }
            uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
            gm_l14 = t110;
            gm_t88rcucg1b7uk t111 = gm_l1;
            void * t112 = gm_l2;
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)t112));
            uint8_t t114 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t113) + 8)); i++) if (gm_eq9m6mvtmgde(((uint8_t *)GM_LD(void *, ((uint8_t *)&t113))) + i * 4, ((uint8_t *)&t111))) { t114 = 1; break; }
            uint8_t t115 = (uint8_t)(t114);
            if (t115) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            void * t116 = gm_l2;
            gm_t88rcucg1b7uk t117 = gm_l1;
            gm_tftb1odc739t5 t118 = gm_f_push_k3l9nic(t116, GM_CONV(gm_csguankko4lmhd, t117));
            gm_l27 = t118;
            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
            if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srckg5f2c0p31ur, 127, 13);
            gm_l15 = ((uint8_t)1);
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, (((uint8_t *)t120) + 1088));
            uint64_t t122 = GM_LD(uint64_t, ((((uint8_t *)t120) + 1088) + 8));
            uint32_t t123 = gm_l14;
            uint64_t t124 = (uint64_t)t123;
            if ((uintptr_t)t124 >= (uintptr_t)(t122)) gm_trap_index((uintptr_t)t124, t122, gm_srckg5f2c0p31ur, 129, 19);
            {
                {
                    uint8_t t125 = GM_LD(uint8_t, (((((uint8_t *)t121) + (uintptr_t)t124 * 184) + 24) + 0));
                    uint8_t t126 = (uint8_t)(t125 == ((uint8_t)0ULL));
                    if (!t126) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&F.l16), (((((uint8_t *)t121) + (uintptr_t)t124 * 184) + 24) + 8), 48);
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                    gm_l17 = t127;
                    gm_l18 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t128 = gm_l18;
                            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t130 = (t128 < t129);
                            if (!t130) {
                                goto gm_lb25;
                            }
                            {
                                void * t131 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint64_t t133 = gm_l18;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l19), (((uint8_t *)t131) + (uintptr_t)t133 * 40), 40);
                                        void * t134 = gm_l0;
                                        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                        void * t136 = gm_l2;
                                        uint8_t t137 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t134, t135, t136);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t138 = (uint8_t)!t137;
                                        if (t138) {
                                            gm_l15 = ((uint8_t)0);
                                            goto gm_lb25;
                                        }
                                        goto gm_lb28;
                                    }
gm_lb29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 130, 33);
                                }
gm_lb28:;
                            }
gm_lb27:;
                            uint64_t t139 = gm_l18;
                            uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                            uint64_t t142 = r140;
                            gm_l18 = t142;
                        }
                    }
gm_lb25:;
                    goto gm_lb23;
                }
gm_lb24:;
                {
                    uint8_t t143 = GM_LD(uint8_t, (((((uint8_t *)t121) + (uintptr_t)t124 * 184) + 24) + 0));
                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)1ULL));
                    if (!t144) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t145 = GM_LD(gm_b16_8, ((((((uint8_t *)t121) + (uintptr_t)t124 * 184) + 24) + 8) + 8));
                    gm_l20 = t145;
                    gm_b16_8 t146 = gm_l20;
                    gm_l21 = t146;
                    gm_l22 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t147 = gm_l22;
                            uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            uint8_t t149 = (t147 < t148);
                            if (!t149) {
                                goto gm_lb31;
                            }
                            {
                                void * t150 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint64_t t152 = gm_l22;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l23), (((uint8_t *)t150) + (uintptr_t)t152 * 72), 72);
                                        {
                                            {
                                                uint8_t t153 = (uint8_t)(((((uint8_t *)&F.l23) + 16))[4] != 0);
                                                if (!t153) {
                                                    goto gm_lb37;
                                                }
                                                gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l23) + 16));
                                                gm_l24 = t154;
                                                void * t155 = gm_l0;
                                                gm_t88rcucg1b7uk t156 = gm_l24;
                                                void * t157 = gm_l2;
                                                uint8_t t158 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t155, t156, t157);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t159 = (uint8_t)!t158;
                                                if (!t159) {
                                                    goto gm_lb37;
                                                }
                                                gm_l15 = ((uint8_t)0);
                                                goto gm_lb31;
                                            }
gm_lb37:;
                                        }
gm_lb36:;
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 136, 40);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t160 = gm_l22;
                            uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                            uint64_t t163 = r161;
                            gm_l22 = t163;
                        }
                    }
gm_lb31:;
                    goto gm_lb23;
                }
gm_lb30:;
                {
                    uint8_t t164 = GM_LD(uint8_t, (((((uint8_t *)t121) + (uintptr_t)t124 * 184) + 24) + 0));
                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)2ULL));
                    if (!t165) {
                        goto gm_lb38;
                    }
                    gm_l15 = ((uint8_t)0);
                    goto gm_lb23;
                }
gm_lb38:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 129, 13);
            }
gm_lb23:;
            void * t166 = gm_l2;
            gm_tkn42u7na3t81 t167 = gm_f_pop_knqt0be(t166);
            uint8_t t168 = gm_l15;
            F.ret = t168;
            goto gm_fret;
        }
gm_lb22:;
        {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb39:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 114, 5);
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
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_ord_1io9knm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* elems */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_tnnjeerolos0a gm_l4; /* kind */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint32_t gm_l7; /* p */
    gm_tep2drsl9408b gm_l8; /* u */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l12;
    gm_t53at80pfdus3 gm_l13;
    uint8_t gm_l14;
    gm_tdlpv0q69lov1 gm_l15;
    uint8_t gm_l16;
    void * t28 = gm_l0;
    void * t29 = (void *)(((uint8_t *)t28) + 8);
    gm_t88rcucg1b7uk t30 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l13, t29, GM_CONV(gm_csguankko4lmhd, t30));
    {
        {
            {
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)4ULL));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                    if (!t36) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)2ULL));
                    if (!t38) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)3ULL));
                    if (!t40) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)9ULL));
                    if (!t42) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l12 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)12ULL));
            if (!t44) {
                goto gm_lb10;
            }
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_l2 = t45;
            void * t46 = gm_l0;
            gm_b16_8 t47 = gm_l2;
            uint8_t t48 = gm_f_all_ord_8bea963(t46, t47);
            gm_l12 = t48;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)11ULL));
            if (!t50) {
                goto gm_lb11;
            }
            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l13) + 8) + 8));
            gm_l3 = t51;
            gm_tnnjeerolos0a t52 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l13) + 8) + 12));
            gm_l4 = t52;
            gm_tnnjeerolos0a t53 = gm_l4;
            gm_tnnjeerolos0a t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)2ULL));
            gm_tnnjeerolos0a t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)3ULL));
            gm_tcf2f71ra2nps t56;
            GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t56 + 0), t54);
            GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t56 + 8), t55);
            uint8_t t57 = 0;
            if (gm_eqb2ul45tidi(((uint8_t *)&t56) + 0, ((uint8_t *)&t53))) t57 = 1;
            if (gm_eqb2ul45tidi(((uint8_t *)&t56) + 8, ((uint8_t *)&t53))) t57 = 1;
            uint8_t t58 = (uint8_t)(!t57);
            if (t58) {
                void * t59 = gm_l0;
                gm_t88rcucg1b7uk t60 = gm_l3;
                uint8_t t61 = gm_f_has_ord_1io9knm(t59, GM_CONV(gm_csguankko4lmhd, t60));
                gm_l14 = t61;
            } else {
                gm_l14 = ((uint8_t)0);
            }
            uint8_t t62 = gm_l14;
            gm_l12 = t62;
            goto gm_lb1;
        }
gm_lb11:;
        {
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)20ULL));
                    if (!t64) {
                        goto gm_lb14;
                    }
                    gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l13) + 8) + 4));
                    gm_l5 = t65;
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)10ULL));
                    if (!t67) {
                        goto gm_lb15;
                    }
                    gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l13) + 8) + 4));
                    gm_l5 = t68;
                    goto gm_lb13;
                }
gm_lb15:;
                goto gm_lb12;
            }
gm_lb13:;
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l5;
            uint8_t t71 = gm_f_has_ord_1io9knm(t69, GM_CONV(gm_csguankko4lmhd, t70));
            gm_l12 = t71;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)19ULL));
            if (!t73) {
                goto gm_lb16;
            }
            uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
            gm_l7 = t74;
            void * t75 = gm_l0;
            uint32_t t76 = gm_l7;
            gm_t9qkh0rsooc8d t77;
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)1ULL));
            uint8_t t78 = gm_f_param_has_2r09oc7(t75, t76, t77);
            gm_l12 = t78;
            goto gm_lb1;
        }
gm_lb16:;
        {
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)17ULL));
            if (!t80) {
                goto gm_lb17;
            }
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l1;
            gm_f_union_variants_a7khaqp((void *)&gm_l15, t81, GM_CONV(gm_csguankko4lmhd, t82));
            uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l15))[24] != 0);
            if (t83) {
                memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l15), 24);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_l9 = t84;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t85 = gm_l10;
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t87 = (t85 < t86);
                    if (!t87) {
                        goto gm_lb18;
                    }
                    {
                        void * t88 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t90 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)t88) + (uintptr_t)t90 * 72), 72);
                                {
                                    {
                                        uint8_t t91 = (uint8_t)(((((uint8_t *)&F.l11) + 16))[4] != 0);
                                        if (t91) {
                                            goto gm_lb24;
                                        }
                                        gm_l16 = ((uint8_t)0);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    {
                                        gm_l16 = ((uint8_t)1);
                                        goto gm_lb23;
                                    }
gm_lb25:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 91, 16);
                                }
gm_lb23:;
                                uint8_t t92 = gm_l16;
                                if (t92) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 90, 9);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t93 = gm_l10;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l10 = t96;
                }
            }
gm_lb18:;
            gm_l12 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb17:;
        {
            gm_l12 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb26:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 81, 62);
    }
gm_lb1:;
    uint8_t t97 = gm_l12;
    F.ret = t97;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_implies_ibnta05(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_implies_ibnta05 F;
    gm_t9qkh0rsooc8d gm_l0; /* a */
    gm_t9qkh0rsooc8d gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t12 = gm_l1;
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            uint8_t t14 = (uint8_t)((t13 == ((uint8_t)0ULL)));
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
            gm_l2 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
            if (!t17) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t18 = gm_l1;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
            gm_t0am46iohlpol t21;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 0), t19);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
            uint8_t t22 = 0;
            if (gm_eqmvsckgkj25(((uint8_t *)&t21) + 0, ((uint8_t *)&t18))) t22 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t21) + 1, ((uint8_t *)&t18))) t22 = 1;
            uint8_t t23 = (uint8_t)(t22);
            gm_l2 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
            if (!t25) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t26 = gm_l1;
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
            gm_tk22flr9mh3ql t30;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 0), t27);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 1), t28);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 2), t29);
            uint8_t t31 = 0;
            if (gm_eqmvsckgkj25(((uint8_t *)&t30) + 0, ((uint8_t *)&t26))) t31 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t30) + 1, ((uint8_t *)&t26))) t31 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t30) + 2, ((uint8_t *)&t26))) t31 = 1;
            uint8_t t32 = (uint8_t)(t31);
            gm_l2 = t32;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
            if (!t34) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t35 = gm_l1;
            gm_t9qkh0rsooc8d t36;
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)3ULL));
            gm_t9qkh0rsooc8d t37;
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t39;
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)6ULL));
            gm_t724v60om7sh5 t41;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 0), t36);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 1), t37);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 2), t38);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 3), t39);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 4), t40);
            uint8_t t42 = 0;
            if (gm_eqmvsckgkj25(((uint8_t *)&t41) + 0, ((uint8_t *)&t35))) t42 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t41) + 1, ((uint8_t *)&t35))) t42 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t41) + 2, ((uint8_t *)&t35))) t42 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t41) + 3, ((uint8_t *)&t35))) t42 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t41) + 4, ((uint8_t *)&t35))) t42 = 1;
            uint8_t t43 = (uint8_t)(t42);
            gm_l2 = t43;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
            if (!t45) {
                goto gm_lb6;
            }
            gm_t9qkh0rsooc8d t46 = gm_l1;
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)4ULL));
            gm_t9qkh0rsooc8d t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t49;
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t50;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)6ULL));
            gm_t724v60om7sh5 t52;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 0), t47);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 1), t48);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 2), t49);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 3), t50);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 4), t51);
            uint8_t t53 = 0;
            if (gm_eqmvsckgkj25(((uint8_t *)&t52) + 0, ((uint8_t *)&t46))) t53 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t52) + 1, ((uint8_t *)&t46))) t53 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t52) + 2, ((uint8_t *)&t46))) t53 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t52) + 3, ((uint8_t *)&t46))) t53 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t52) + 4, ((uint8_t *)&t46))) t53 = 1;
            uint8_t t54 = (uint8_t)(t53);
            gm_l2 = t54;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)5ULL));
            if (!t56) {
                goto gm_lb7;
            }
            gm_t9qkh0rsooc8d t57 = gm_l1;
            gm_t9qkh0rsooc8d t58;
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)5ULL));
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)4ULL));
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t61;
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)6ULL));
            gm_t9rbkbpjtolar t64;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 0), t58);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 1), t59);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 2), t60);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 3), t61);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 4), t62);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 5), t63);
            uint8_t t65 = 0;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 0, ((uint8_t *)&t57))) t65 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 1, ((uint8_t *)&t57))) t65 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 2, ((uint8_t *)&t57))) t65 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 3, ((uint8_t *)&t57))) t65 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 4, ((uint8_t *)&t57))) t65 = 1;
            if (gm_eqmvsckgkj25(((uint8_t *)&t64) + 5, ((uint8_t *)&t57))) t65 = 1;
            uint8_t t66 = (uint8_t)(t65);
            gm_l2 = t66;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)6ULL));
            if (!t68) {
                goto gm_lb8;
            }
            gm_t9qkh0rsooc8d t69 = gm_l1;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
            uint8_t t71 = (uint8_t)((t70 == ((uint8_t)6ULL)));
            gm_t9qkh0rsooc8d t72;
            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)6ULL));
            gm_l2 = t71;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 17, 44);
    }
gm_lb1:;
    uint8_t t73 = gm_l2;
    F.ret = t73;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_inner_second_class_part_ufhro3f(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inner_second_class_part_ufhro3f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* x */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_t88rcucg1b7uk gm_l8; /* x */
    gm_t88rcucg1b7uk gm_l9; /* err */
    gm_t88rcucg1b7uk gm_l10; /* ok */
    gm_b16_8 gm_l11; /* elems */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t88rcucg1b7uk gm_l14; /* x */
    gm_t88rcucg1b7uk gm_l15; /* p */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l20; /* x */
    gm_t88rcucg1b7uk gm_l21; /* p */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l22;
    gm_tkn42u7na3t81 gm_l23;
    gm_tkn42u7na3t81 gm_l24;
    gm_tkn42u7na3t81 gm_l25;
    gm_tkn42u7na3t81 gm_l26;
    void * t38 = gm_l0;
    void * t39 = (void *)(((uint8_t *)t38) + 8);
    gm_t88rcucg1b7uk t40 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l22, t39, GM_CONV(gm_csguankko4lmhd, t40));
    {
        {
            {
                {
                    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)25ULL));
                    if (!t42) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)6ULL));
                    if (!t44) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_t88rcucg1b7uk t45 = gm_l1;
            gm_tkn42u7na3t81 t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t46), t45);
            ((uint8_t *)&t46)[4] = 1;
            F.ret = t46;
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)8ULL));
                    if (!t48) {
                        goto gm_lb8;
                    }
                    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                    gm_l2 = t49;
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)9ULL));
                    if (!t51) {
                        goto gm_lb9;
                    }
                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                    gm_l2 = t52;
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)10ULL));
                    if (!t54) {
                        goto gm_lb10;
                    }
                    gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                    gm_l2 = t55;
                    goto gm_lb7;
                }
gm_lb10:;
                {
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t57 = (uint8_t)(t56 == ((uint8_t)13ULL));
                    if (!t57) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
                    gm_l2 = t58;
                    goto gm_lb7;
                }
gm_lb11:;
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)18ULL));
                    if (!t60) {
                        goto gm_lb12;
                    }
                    gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
                    gm_l2 = t61;
                    goto gm_lb7;
                }
gm_lb12:;
                goto gm_lb6;
            }
gm_lb7:;
            void * t62 = gm_l0;
            gm_t88rcucg1b7uk t63 = gm_l2;
            gm_tkn42u7na3t81 t64 = gm_f_inner_second_class_part_ufhro3f(t62, GM_CONV(gm_csguankko4lmhd, t63));
            F.ret = t64;
            goto gm_fret;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)11ULL));
                    if (!t66) {
                        goto gm_lb15;
                    }
                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 8));
                    gm_l7 = t67;
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)20ULL));
                    if (!t69) {
                        goto gm_lb16;
                    }
                    gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                    gm_l7 = t70;
                    goto gm_lb14;
                }
gm_lb16:;
                goto gm_lb13;
            }
gm_lb14:;
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l7;
            gm_tkn42u7na3t81 t73 = gm_f_inner_second_class_part_ufhro3f(t71, GM_CONV(gm_csguankko4lmhd, t72));
            F.ret = t73;
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)14ULL));
            if (!t75) {
                goto gm_lb17;
            }
            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
            gm_l9 = t76;
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
            gm_l10 = t77;
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l9;
            gm_tkn42u7na3t81 t80 = gm_f_inner_second_class_part_ufhro3f(t78, GM_CONV(gm_csguankko4lmhd, t79));
            gm_l24 = t80;
            uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
            if (t81) {
                gm_tkn42u7na3t81 t82 = gm_l24;
                gm_l23 = t82;
            } else {
                void * t83 = gm_l0;
                gm_t88rcucg1b7uk t84 = gm_l10;
                gm_tkn42u7na3t81 t85 = gm_f_inner_second_class_part_ufhro3f(t83, GM_CONV(gm_csguankko4lmhd, t84));
                gm_l23 = t85;
            }
            gm_tkn42u7na3t81 t86 = gm_l23;
            F.ret = t86;
            goto gm_fret;
        }
gm_lb17:;
        {
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)12ULL));
            if (!t88) {
                goto gm_lb18;
            }
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_l11 = t89;
            gm_b16_8 t90 = gm_l11;
            gm_l12 = t90;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t91 = gm_l13;
                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t93 = (t91 < t92);
                    if (!t93) {
                        goto gm_lb19;
                    }
                    {
                        void * t94 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t96 = gm_l13;
                        {
                            {
                                gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + (uintptr_t)t96 * 4));
                                gm_l14 = t97;
                                {
                                    {
                                        void * t98 = gm_l0;
                                        gm_t88rcucg1b7uk t99 = gm_l14;
                                        gm_tkn42u7na3t81 t100 = gm_f_inner_second_class_part_ufhro3f(t98, GM_CONV(gm_csguankko4lmhd, t99));
                                        gm_l25 = t100;
                                        uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l25))[4] != 0);
                                        if (!t101) {
                                            goto gm_lb25;
                                        }
                                        gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l25));
                                        gm_l15 = t102;
                                        gm_t88rcucg1b7uk t103 = gm_l15;
                                        gm_tkn42u7na3t81 t104;
                                        GM_ST(uint8_t, (uint8_t *)&t104 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t104 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t104), t103);
                                        ((uint8_t *)&t104)[4] = 1;
                                        F.ret = t104;
                                        goto gm_fret;
                                    }
gm_lb25:;
                                }
gm_lb24:;
                                goto gm_lb22;
                            }
gm_lb23:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 212, 13);
                        }
gm_lb22:;
                    }
gm_lb21:;
                    uint64_t t105 = gm_l13;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l13 = t108;
                }
            }
gm_lb19:;
            gm_tkn42u7na3t81 t109;
            memset(&t109, 0, sizeof t109);
            F.ret = t109;
            goto gm_fret;
        }
gm_lb18:;
        {
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)17ULL));
            if (!t111) {
                goto gm_lb26;
            }
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l22) + 8) + 8));
            gm_l16 = t112;
            gm_b16_8 t113 = gm_l16;
            gm_l17 = t113;
            gm_l18 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t114 = gm_l18;
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint8_t t116 = (t114 < t115);
                    if (!t116) {
                        goto gm_lb27;
                    }
                    {
                        void * t117 = GM_LD(void *, ((uint8_t *)&gm_l17));
                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                        uint64_t t119 = gm_l18;
                        {
                            {
                                memmove(((uint8_t *)&F.l19), (((uint8_t *)t117) + (uintptr_t)t119 * 40), 40);
                                {
                                    {
                                        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 0));
                                        uint8_t t121 = (uint8_t)(t120 == ((uint8_t)0ULL));
                                        if (!t121) {
                                            goto gm_lb33;
                                        }
                                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 8));
                                        gm_l20 = t122;
                                        {
                                            {
                                                void * t123 = gm_l0;
                                                gm_t88rcucg1b7uk t124 = gm_l20;
                                                gm_tkn42u7na3t81 t125 = gm_f_inner_second_class_part_ufhro3f(t123, GM_CONV(gm_csguankko4lmhd, t124));
                                                gm_l26 = t125;
                                                uint8_t t126 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                                                if (!t126) {
                                                    goto gm_lb35;
                                                }
                                                gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
                                                gm_l21 = t127;
                                                gm_t88rcucg1b7uk t128 = gm_l21;
                                                gm_tkn42u7na3t81 t129;
                                                GM_ST(uint8_t, (uint8_t *)&t129 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t129 + 6, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t129), t128);
                                                ((uint8_t *)&t129)[4] = 1;
                                                F.ret = t129;
                                                goto gm_fret;
                                            }
gm_lb35:;
                                        }
gm_lb34:;
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                goto gm_lb30;
                            }
gm_lb31:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 218, 13);
                        }
gm_lb30:;
                    }
gm_lb29:;
                    uint64_t t130 = gm_l18;
                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                    uint64_t t133 = r131;
                    gm_l18 = t133;
                }
            }
gm_lb27:;
            gm_tkn42u7na3t81 t134;
            memset(&t134, 0, sizeof t134);
            F.ret = t134;
            goto gm_fret;
        }
gm_lb26:;
        {
            gm_tkn42u7na3t81 t135;
            memset(&t135, 0, sizeof t135);
            F.ret = t135;
            goto gm_fret;
        }
gm_lb36:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 206, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_instantiate_35r5pb2 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* id */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l18; /* kind */
    uint32_t gm_l19; /* id */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t88rcucg1b7uk gm_l22; /* a */
    uint32_t gm_l23; /* depth */
    GM_STACK_CHECK(&F, 1723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l24;
    gm_tn68el8d14vku gm_l25;
    gm_tftb1odc739t5 gm_l26;
    uint8_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 1264);
            uint32_t t24 = gm_l1;
            gm_b16_8 t25 = gm_l2;
            gm_tiilmfd16v3fc t26;
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t24);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
            gm_t9805qtonbsag t27 = gm_f_get_6qnso5a(t23, &t26);
            gm_l24 = t27;
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
            if (!t28) {
                goto gm_lb2;
            }
            uint32_t t29 = GM_LD(uint32_t, ((uint8_t *)&gm_l24));
            gm_l4 = t29;
            uint32_t t30 = gm_l4;
            F.ret = t30;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 344));
        uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 344) + 8));
        uint32_t t34 = gm_l1;
        uint64_t t35 = (uint64_t)t34;
        if ((uintptr_t)t35 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t35, t33, gm_srckg5f2c0p31ur, 296, 12);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t32) + (uintptr_t)t35 * 136), 136);
    }
    {
        gm_trqfvmjtt4sij t36;
        gm_f_list_oalq03d((void *)&t36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t36;
    }
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
    gm_l7 = t38;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t39 = gm_l8;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t41 = (t39 < t40);
            if (!t41) {
                goto gm_lb3;
            }
            {
                uint64_t t42 = gm_l8;
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t45 = gm_l8;
                uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)t43) + (uintptr_t)t45 * 4));
                gm_tn68el8d14vku t47;
                GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t42);
                GM_ST(uint32_t, ((uint8_t *)&t47 + 8), t46);
                gm_l25 = t47;
                {
                    {
                        uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l25));
                        gm_l9 = t48;
                        uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
                        gm_l10 = t49;
                        void * t50 = (void *)((uint8_t *)&F.l6);
                        uint32_t t51 = gm_l10;
                        void * t52 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t54 = gm_l9;
                        if ((uintptr_t)t54 >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)t54, t53, gm_srckg5f2c0p31ur, 298, 72);
                        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + (uintptr_t)t54 * 4));
                        gm_tplpbqf3duvn3 t56;
                        GM_ST(uint32_t, ((uint8_t *)&t56 + 0), t51);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56 + 4), t55);
                        gm_tftb1odc739t5 t57 = gm_f_push_h69n5fi(t50, GM_CONV(gm_cs7e9ppsd384bj, t56));
                        gm_l26 = t57;
                        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srckg5f2c0p31ur, 298, 43);
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 298, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t59 = gm_l8;
            uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
            uint64_t t62 = r60;
            gm_l8 = t62;
        }
    }
gm_lb3:;
    {
        gm_trqfvmjtt4sij t63;
        gm_f_list_af74s0q((void *)&t63, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t63;
    }
    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
    gm_l12 = t64;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t65 = gm_l13;
            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t67 = (t65 < t66);
            if (!t67) {
                goto gm_lb8;
            }
            {
                void * t68 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t70 = gm_l13;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t68) + (uintptr_t)t70 * 56), 56);
                        }
                        {
                            {
                                uint8_t t71 = (uint8_t)(((((uint8_t *)&F.l14) + 40))[4] != 0);
                                if (t71) {
                                    goto gm_lb14;
                                }
                                gm_l27 = ((uint8_t)0);
                                goto gm_lb13;
                            }
gm_lb14:;
                            {
                                gm_l27 = ((uint8_t)1);
                                goto gm_lb13;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 301, 12);
                        }
gm_lb13:;
                        uint8_t t72 = gm_l27;
                        if (t72) {
                            goto gm_lb10;
                        }
                        {
                            memmove(((uint8_t *)&F.l15), ((uint8_t *)&F.l14), 56);
                        }
                        void * t73 = gm_l0;
                        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                        gm_t88rcucg1b7uk t77 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t73, t74, t76);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16), t77);
                        {
                            void * t78 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t79 = gm_f_push_o8u01sh(t78, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l15)));
                            gm_l28 = t79;
                        }
                        uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srckg5f2c0p31ur, 304, 9);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 300, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t81 = gm_l13;
            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
            uint64_t t84 = r82;
            gm_l13 = t84;
        }
    }
gm_lb8:;
    {
        gm_trqfvmjtt4sij t85;
        gm_f_list_nof7vfj((void *)&t85, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l16 = t85;
    }
    void * t86 = (void *)((uint8_t *)&F.l16);
    gm_b16_8 t87 = gm_l2;
    gm_tftb1odc739t5 t88 = gm_f_extend_lfqasgq(t86, t87);
    gm_l29 = t88;
    uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srckg5f2c0p31ur, 307, 5);
    {
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        void * t94 = gm_l0;
        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
        gm_t88rcucg1b7uk t98 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t94, t95, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t99 = gm_l0;
        void * t100 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
        gm_tnpifv20irnph t103 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t99, t100, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t104 = gm_f_empty_slice_d914i5u();
        uint32_t t105 = gm_l1;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
        gm_tiilmfd16v3fc t108;
        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t108 + 0), t105);
        GM_ST(gm_b16_8, ((uint8_t *)&t108 + 8), t107);
        gm_tqu3m2l3n8mnc t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t109), t108);
        ((uint8_t *)&t109)[24] = 1;
        GM_ST(uint32_t, (uint8_t *)&F.l17 + 28, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l17 + 60, 0);
        GM_ST(uint8_t, (uint8_t *)&F.l17 + 129, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l17 + 130, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l17 + 132, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l17 + 0), t90);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&F.l17 + 16), t91);
        GM_ST(uint64_t, ((uint8_t *)&F.l17 + 32), t92);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l17 + 40), t93);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l17 + 56), t98);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&F.l17 + 64), t103);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l17 + 80), t104);
        GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&F.l17 + 96), t109);
        GM_ST(uint8_t, ((uint8_t *)&F.l17 + 128), ((uint8_t)0));
    }
    void * t110 = gm_l0;
    gm_tl874t2atnlh3 t111 = gm_l3;
    gm_b0_1 t112 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t110, t111);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t113 = gm_l0;
    void * t114 = GM_LD(void *, (((uint8_t *)t113) + 384));
    uint64_t t115 = GM_LD(uint64_t, ((((uint8_t *)t113) + 384) + 8));
    uint32_t t116 = gm_l1;
    uint64_t t117 = (uint64_t)t116;
    if ((uintptr_t)t117 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t117, t115, gm_srckg5f2c0p31ur, 319, 27);
    gm_t9qkh0rsooc8d t118 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t114) + (uintptr_t)t117 * 64));
    uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t118) + 0));
    uint8_t t120 = (uint8_t)((t119 == ((uint8_t)2ULL)));
    gm_t9qkh0rsooc8d t121;
    GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)2ULL));
    if (t120) {
        gm_t9qkh0rsooc8d t122;
        GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)2ULL));
        gm_l18 = t122;
    } else {
        gm_t9qkh0rsooc8d t123;
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)0ULL));
        gm_l18 = t123;
    }
    {
        void * t124 = gm_l0;
        gm_t9qkh0rsooc8d t125 = gm_l18;
        gm_tqqhoivahirr0 t126 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)&F.l17));
        gm_tdgfduqjheqdm t127;
        gm_f_bodiless_def_u9kqo09((void *)&t127, t125);
        uint32_t t128 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t124, &t126, &t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t128;
    }
    {
        void * t129 = gm_l0;
        void * t130 = (void *)(((uint8_t *)t129) + 1264);
        uint32_t t131 = gm_l1;
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
        gm_tiilmfd16v3fc t134;
        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t134 + 0), t131);
        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 8), t133);
        uint32_t t135 = gm_l19;
        gm_tftb1odc739t5 t136 = gm_f_put_f1iksvi(t130, &t134, t135);
        gm_l30 = t136;
    }
    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srckg5f2c0p31ur, 321, 5);
    gm_b16_8 t138 = gm_l2;
    gm_l20 = t138;
    gm_l21 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t139 = gm_l21;
            uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
            uint8_t t141 = (t139 < t140);
            if (!t141) {
                goto gm_lb16;
            }
            {
                void * t142 = GM_LD(void *, ((uint8_t *)&gm_l20));
                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint64_t t144 = gm_l21;
                {
                    {
                        gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + (uintptr_t)t144 * 4));
                        gm_l22 = t145;
                        void * t146 = gm_l0;
                        void * t147 = (void *)(((uint8_t *)t146) + 8);
                        gm_t88rcucg1b7uk t148 = gm_l22;
                        uint8_t t149 = gm_f_has_param_u9t0efs(t147, GM_CONV(gm_csguankko4lmhd, t148));
                        if (t149) {
                            uint32_t t150 = gm_l19;
                            F.ret = t150;
                            goto gm_fret;
                        }
                        goto gm_lb19;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 322, 5);
                }
gm_lb19:;
            }
gm_lb18:;
            uint64_t t151 = gm_l21;
            uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
            uint64_t t154 = r152;
            gm_l21 = t154;
        }
    }
gm_lb16:;
    void * t155 = gm_l0;
    uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)t155) + 1360));
    uint32_t r157; bool o158 = __builtin_add_overflow(t156, ((uint32_t)1ULL), &r157);
    if (o158) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 325, 14);
    uint32_t t159 = r157;
    gm_l23 = t159;
    uint32_t t160 = gm_l23;
    uint32_t t161 = GM_LD(uint32_t, ((uint8_t *)gm_cem22767mo4));
    uint8_t t162 = (t160 > t161);
    if (t162) {
        void * t163 = gm_l0;
        gm_tl874t2atnlh3 t164 = gm_l3;
        uint32_t t165 = GM_LD(uint32_t, ((uint8_t *)gm_cem22767mo4));
        gm_t88rcucg1b7uk t166;
        GM_ST(uint32_t, ((uint8_t *)&t166 + 0), t165);
        gm_f_format_hfu2i92((void *)&gm_l31, (void *)NULL, cx, NULL, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srckg5f2c0p31ur, 327, 23);
        gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
        gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
        gm_b0_1 t170 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t163, t164, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t171 = gm_l19;
        F.ret = t171;
        goto gm_fret;
    }
    {
        void * t172 = gm_l0;
        void * t173 = (void *)(((uint8_t *)t172) + 1320);
        uint32_t t174 = gm_l1;
        uint32_t t175 = gm_l19;
        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
        gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
        gm_tl874t2atnlh3 t178 = gm_l3;
        uint32_t t179 = gm_l23;
        gm_t0ut88vjdgrt1 t180;
        GM_ST(uint32_t, ((uint8_t *)&t180 + 0), t174);
        GM_ST(uint32_t, ((uint8_t *)&t180 + 4), t175);
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 8), t177);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t180 + 24), t178);
        GM_ST(uint32_t, ((uint8_t *)&t180 + 36), t179);
        gm_tftb1odc739t5 t181 = gm_f_push_1es0np6(t173, &t180);
        gm_l32 = t181;
    }
    uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
    if (!t182) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srckg5f2c0p31ur, 330, 5);
    uint32_t t183 = gm_l19;
    F.ret = t183;
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
inline __attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_key_dmcraup F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint64_t gm_l3; /* size */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* low */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tbccvikg6b96e gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    gm_tftb1odc739t5 gm_l11;
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
    gm_l3 = t9;
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t10;
    }
    {
        memmove(((uint8_t *)&F.l5), ((uint8_t *)&F.l2), 48);
    }
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t11 = gm_l3;
    gm_l6 = t11;
    {
        for (;;) {
            uint64_t t12 = gm_l7;
            uint64_t t13 = gm_l6;
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                {
                    {
                        {
                            gm_tbci5ospk5nnn t15 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
                            gm_tbci5ospk5nnn t16;
                            gm_f_from_u64_ud13q5v((void *)&t16, (void *)NULL, cx, NULL, ((uint64_t)255ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbci5ospk5nnn t17;
                            gm_f_bit_and_3tgam8m((void *)&t17, (void *)NULL, cx, NULL, &t15, &t16);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf5srh4mp4b3i t18 = gm_f_to_u64_m01t1da(&t17);
                            gm_l10 = t18;
                        }
                        uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                        if (t19) {
                            uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            gm_l8 = t20;
                        } else {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        uint64_t t22 = gm_l8;
                        uint8_t t23 = (uint8_t)t22;
                        gm_tftb1odc739t5 t24 = gm_f_push_t83rg2m(t21, t23);
                        gm_l11 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srckg5f2c0p31ur, 476, 9);
                        {
                            gm_tbci5ospk5nnn t26 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l5));
                            gm_f_shr_4jp99a9((void *)&F.l5, (void *)NULL, cx, NULL, &t26, ((uint64_t)8ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 474, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t27 = gm_l7;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            uint64_t t30 = r28;
            gm_l7 = t30;
        }
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t31 = gm_l1;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_tcmkompbl09il t34;
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 0), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 8), t33);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t428ue2u3hf5g *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t428ue2u3hf5g *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t428ue2u3hf5g *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t428ue2u3hf5g *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&(*(gm_t428ue2u3hf5g *)gm_out) + 8), t34);
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
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_plain_n60iihm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* elems */
    uint32_t gm_l5; /* p */
    uint32_t gm_l6; /* id */
    GM_STACK_CHECK(&F, 275u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l8;
    gm_t53at80pfdus3 gm_l9;
    uint8_t gm_l10;
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    gm_t88rcucg1b7uk t22 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l9, t21, GM_CONV(gm_csguankko4lmhd, t22));
    {
        {
            {
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)1ULL));
                    if (!t26) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)2ULL));
                    if (!t28) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)3ULL));
                    if (!t30) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l8 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)11ULL));
                    if (!t32) {
                        goto gm_lb10;
                    }
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l9) + 8) + 8));
                    gm_l2 = t33;
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)20ULL));
                    if (!t35) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l9) + 8) + 4));
                    gm_l2 = t36;
                    goto gm_lb9;
                }
gm_lb11:;
                goto gm_lb8;
            }
gm_lb9:;
            void * t37 = gm_l0;
            gm_t88rcucg1b7uk t38 = gm_l2;
            uint8_t t39 = gm_f_is_plain_n60iihm(t37, GM_CONV(gm_csguankko4lmhd, t38));
            gm_l8 = t39;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)12ULL));
            if (!t41) {
                goto gm_lb12;
            }
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_l4 = t42;
            void * t43 = gm_l0;
            gm_b16_8 t44 = gm_l4;
            uint8_t t45 = gm_f_all_plain_6k5r6vb(t43, t44);
            gm_l8 = t45;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)19ULL));
            if (!t47) {
                goto gm_lb13;
            }
            uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
            gm_l5 = t48;
            void * t49 = gm_l0;
            uint32_t t50 = gm_l5;
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)6ULL));
            uint8_t t52 = gm_f_param_has_2r09oc7(t49, t50, t51);
            gm_l8 = t52;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)17ULL));
            if (!t54) {
                goto gm_lb14;
            }
            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
            gm_l6 = t55;
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 1088));
            uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)t56) + 1088) + 8));
            uint32_t t59 = gm_l6;
            uint64_t t60 = (uint64_t)t59;
            if ((uintptr_t)t60 >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)t60, t58, gm_srckg5f2c0p31ur, 156, 26);
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, (((((uint8_t *)t57) + (uintptr_t)t60 * 184) + 24) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                    if (!t62) {
                        goto gm_lb16;
                    }
                    {
                        memmove(((uint8_t *)&F.l7), (((((uint8_t *)t57) + (uintptr_t)t60 * 184) + 24) + 8), 48);
                    }
                    {
                        void * t63 = gm_l0;
                        uint8_t t64 = gm_f_plain_fields_i69c54j(t63, &(*(gm_tpautr4br7h3l *)((uint8_t *)&F.l7)));
                        gm_l10 = t64;
                    }
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    gm_l10 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb17:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 156, 20);
            }
gm_lb15:;
            uint8_t t65 = gm_l10;
            gm_l8 = t65;
            goto gm_lb1;
        }
gm_lb14:;
        {
            gm_l8 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb18:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 151, 63);
    }
gm_lb1:;
    uint8_t t66 = gm_l8;
    F.ret = t66;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_key_int_pqscuvn F;
    void * gm_l0; /* s */
    gm_t428ue2u3hf5g gm_l1; /* k */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 554u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l1) + 8));
            gm_l2 = t11;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l1) + 8) + 8));
            gm_l3 = t12;
            void * t13 = gm_l0;
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            gm_t88rcucg1b7uk t15 = gm_l2;
            uint8_t t16 = gm_f_is_integer_8d4f9ek(t14, GM_CONV(gm_csguankko4lmhd, t15));
            uint8_t t17 = (uint8_t)!t16;
            if (t17) {
                memset(&(*(gm_thrdpodrs8pgt *)gm_out), 0, sizeof (*(gm_thrdpodrs8pgt *)gm_out));
                goto gm_fdone;
            }
            {
                gm_f_zero_s25l990((void *)&F.l4, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l5 = t18;
            {
                for (;;) {
                    uint64_t t19 = gm_l5;
                    uint8_t t20 = (t19 > ((uint64_t)0ULL));
                    if (!t20) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t21 = gm_l5;
                        uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
                        if (o23) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 461, 9);
                        uint64_t t24 = r22;
                        gm_l5 = t24;
                        {
                            gm_tbci5ospk5nnn t25;
                            gm_f_shl_qqecl81((void *)&t25, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), ((uint64_t)8ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t26 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t28 = gm_l5;
                            if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srckg5f2c0p31ur, 462, 46);
                            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
                            uint64_t t30 = (uint64_t)t29;
                            gm_tbci5ospk5nnn t31;
                            gm_f_from_u64_ud13q5v((void *)&t31, (void *)NULL, cx, NULL, t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_f_add_420lj6o((void *)&F.l4, (void *)NULL, cx, NULL, &t25, &t31);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            {
                {
                    void * t32 = gm_l0;
                    void * t33 = (void *)(((uint8_t *)t32) + 8);
                    gm_t88rcucg1b7uk t34 = gm_l2;
                    uint8_t t35 = gm_f_is_signed_integer_s66djm7(t33, GM_CONV(gm_csguankko4lmhd, t34));
                    if (!t35) {
                        goto gm_lb7;
                    }
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t37 = (t36 > ((uint64_t)0ULL));
                    if (!t37) {
                        goto gm_lb7;
                    }
                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 464, 55);
                    uint64_t t43 = r41;
                    if ((uintptr_t)t43 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t43, t39, gm_srckg5f2c0p31ur, 464, 53);
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t43 * 1));
                    uint8_t t45 = (t44 >= ((uint8_t)128ULL));
                    if (!t45) {
                        goto gm_lb7;
                    }
                    {
                        gm_tbci5ospk5nnn t46 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l4));
                        gm_tbci5ospk5nnn t47;
                        gm_f_one_08qg1op((void *)&t47, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t r49; bool o50 = __builtin_mul_overflow(t48, ((uint64_t)8ULL), &r49);
                        if (o50) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 464, 106);
                        uint64_t t51 = r49;
                        uint64_t t52 = (uint64_t)t51;
                        gm_tbci5ospk5nnn t53;
                        gm_f_shl_qqecl81((void *)&t53, (void *)NULL, cx, NULL, &t47, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_sub_pktm5cr((void *)&F.l4, (void *)NULL, cx, NULL, &t46, &t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            {
                gm_tbci5ospk5nnn t54 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l4));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 52, 0);
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out)), t54);
                ((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out))[48] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        memset(&(*(gm_thrdpodrs8pgt *)gm_out), 0, sizeof (*(gm_thrdpodrs8pgt *)gm_out));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_match_effects_0l1vbuc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_match_effects_0l1vbuc F;
    void * gm_l0; /* s */
    void * gm_l1; /* pat */
    void * gm_l2; /* actual */
    void * gm_l3; /* binds */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* p */
    gm_tqu3m2l3n8mnc gm_l7; /* found */
    int32_t gm_l8; /* count */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tiilmfd16v3fc gm_l11; /* a */
    gm_tiilmfd16v3fc gm_l12; /* a */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l17; /* xt */
    gm_t88rcucg1b7uk gm_l18; /* yt */
    gm_tqu3m2l3n8mnc gm_l19; /* splice */
    int32_t gm_l20; /* splices */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tiilmfd16v3fc gm_l23; /* p */
    gm_tiilmfd16v3fc gm_l24; /* sp */
    gm_t88rcucg1b7uk gm_l25; /* t */
    uint32_t gm_l26; /* e */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_tiilmfd16v3fc gm_l30; /* a */
    uint8_t gm_l31; /* named */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tiilmfd16v3fc gm_l34; /* p */
    GM_STACK_CHECK(&F, 1600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l35;
    gm_tdr8bh2bm3fc8 gm_l36;
    gm_t53at80pfdus3 gm_l37;
    uint8_t gm_l38;
    gm_tkn42u7na3t81 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    {
        void * t43 = gm_l1;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)t43));
        gm_l4 = t44;
        gm_l5 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t45 = gm_l5;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t47 = (t45 < t46);
                if (!t47) {
                    goto gm_lb1;
                }
                {
                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t50 = gm_l5;
                    {
                        {
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)t48) + (uintptr_t)t50 * 24), 24);
                            }
                            uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                            uint8_t t52 = ((t51 == ((uint32_t)0ULL)));
                            if (t52) {
                                gm_l35 = ((uint8_t)1);
                            } else {
                                uint64_t t53 = GM_LD(uint64_t, ((((uint8_t *)&gm_l6) + 8) + 8));
                                uint8_t t54 = ((t53 == ((uint64_t)0ULL)));
                                gm_l35 = t54;
                            }
                            uint8_t t55 = gm_l35;
                            if (t55) {
                                goto gm_lb3;
                            }
                            {
                                memset(&gm_l7, 0, sizeof gm_l7);
                            }
                            gm_l8 = ((int32_t)0LL);
                            void * t56 = gm_l2;
                            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)t56));
                            gm_l9 = t57;
                            gm_l10 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t58 = gm_l10;
                                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                    uint8_t t60 = (t58 < t59);
                                    if (!t60) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t61 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t63 = gm_l10;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l11), (((uint8_t *)t61) + (uintptr_t)t63 * 24), 24);
                                                uint32_t t64 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                                                uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                                uint8_t t66 = ((t64 == t65));
                                                if (t66) {
                                                    {
                                                        gm_tiilmfd16v3fc t67 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l11));
                                                        GM_ST(uint8_t, (uint8_t *)&gm_l7 + 25, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&gm_l7 + 26, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&gm_l7 + 28, 0);
                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l7), t67);
                                                        ((uint8_t *)&gm_l7)[24] = 1;
                                                    }
                                                    int32_t t68 = gm_l8;
                                                    int32_t r69; bool o70 = __builtin_add_overflow(t68, ((int32_t)1LL), &r69);
                                                    if (o70) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 420, 17);
                                                    int32_t t71 = r69;
                                                    gm_l8 = t71;
                                                }
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 417, 9);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t72 = gm_l10;
                                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                    uint64_t t75 = r73;
                                    gm_l10 = t75;
                                }
                            }
gm_lb6:;
                            int32_t t76 = gm_l8;
                            uint8_t t77 = (!(t76 == ((int32_t)1LL)));
                            if (t77) {
                                goto gm_lb3;
                            }
                            uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l7))[24] != 0);
                            if (t78) {
                                memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l7), 24);
                            } else {
                                goto gm_lb3;
                            }
                            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                            gm_l13 = t79;
                            gm_l14 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t80 = gm_l14;
                                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint8_t t82 = (t80 < t81);
                                    if (!t82) {
                                        goto gm_lb11;
                                    }
                                    {
                                        {
                                            uint64_t t83 = gm_l14;
                                            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                            uint64_t t86 = gm_l14;
                                            gm_toa2vhe3l4hfr t87 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t84) + (uintptr_t)t86 * 40));
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l36 + 0), t83);
                                            GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&gm_l36 + 8), t87);
                                        }
                                        {
                                            {
                                                uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                                gm_l15 = t88;
                                                memmove(((uint8_t *)&F.l16), (((uint8_t *)&gm_l36) + 8), 40);
                                                {
                                                    {
                                                        uint64_t t89 = gm_l15;
                                                        uint64_t t90 = GM_LD(uint64_t, ((((uint8_t *)&gm_l12) + 8) + 8));
                                                        uint8_t t91 = (t89 < t90);
                                                        if (!t91) {
                                                            goto gm_lb17;
                                                        }
                                                        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                                        uint8_t t93 = (uint8_t)(t92 == ((uint8_t)0ULL));
                                                        if (!t93) {
                                                            goto gm_lb17;
                                                        }
                                                        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 8));
                                                        gm_l17 = t94;
                                                        void * t95 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                                                        uint64_t t96 = GM_LD(uint64_t, ((((uint8_t *)&gm_l12) + 8) + 8));
                                                        uint64_t t97 = gm_l15;
                                                        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t95) + (uintptr_t)t97 * 40) + 0));
                                                        uint8_t t99 = (uint8_t)(t98 == ((uint8_t)0ULL));
                                                        if (!t99) {
                                                            goto gm_lb17;
                                                        }
                                                        gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t95) + (uintptr_t)t97 * 40) + 8));
                                                        gm_l18 = t100;
                                                        void * t101 = gm_l0;
                                                        gm_t88rcucg1b7uk t102 = gm_l17;
                                                        gm_t88rcucg1b7uk t103 = gm_l18;
                                                        void * t104 = gm_l3;
                                                        gm_b0_1 t105 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t101, t102, t103, t104);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                }
gm_lb16:;
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 425, 9);
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    uint64_t t106 = gm_l14;
                                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                    uint64_t t109 = r107;
                                    gm_l14 = t109;
                                }
                            }
gm_lb11:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 413, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t110 = gm_l5;
                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                uint64_t t113 = r111;
                gm_l5 = t113;
            }
        }
gm_lb1:;
    }
    {
        memset(&gm_l19, 0, sizeof gm_l19);
        gm_l20 = ((int32_t)0LL);
        void * t114 = gm_l1;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)t114));
        gm_l21 = t115;
        gm_l22 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t116 = gm_l22;
                uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint8_t t118 = (t116 < t117);
                if (!t118) {
                    goto gm_lb18;
                }
                {
                    void * t119 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t121 = gm_l22;
                    {
                        {
                            memmove(((uint8_t *)&gm_l23), (((uint8_t *)t119) + (uintptr_t)t121 * 24), 24);
                            uint32_t t122 = GM_LD(uint32_t, ((uint8_t *)&gm_l23));
                            uint8_t t123 = ((t122 == ((uint32_t)0ULL)));
                            if (t123) {
                                {
                                    gm_tiilmfd16v3fc t124 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l23));
                                    GM_ST(uint8_t, (uint8_t *)&gm_l19 + 25, 0);
                                    GM_ST(uint16_t, (uint8_t *)&gm_l19 + 26, 0);
                                    GM_ST(uint32_t, (uint8_t *)&gm_l19 + 28, 0);
                                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l19), t124);
                                    ((uint8_t *)&gm_l19)[24] = 1;
                                }
                                int32_t t125 = gm_l20;
                                int32_t r126; bool o127 = __builtin_add_overflow(t125, ((int32_t)1LL), &r126);
                                if (o127) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 434, 13);
                                int32_t t128 = r126;
                                gm_l20 = t128;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 431, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t129 = gm_l22;
                uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                uint64_t t132 = r130;
                gm_l22 = t132;
            }
        }
gm_lb18:;
        int32_t t133 = gm_l20;
        uint8_t t134 = (!(t133 == ((int32_t)1LL)));
        if (t134) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l19))[24] != 0);
        if (t135) {
            memmove(((uint8_t *)&gm_l24), ((uint8_t *)&gm_l19), 24);
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t136 = GM_LD(uint64_t, ((((uint8_t *)&gm_l24) + 8) + 8));
        uint8_t t137 = ((t136 == ((uint64_t)0ULL)));
        if (t137) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        {
            {
                void * t138 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
                uint64_t t139 = GM_LD(uint64_t, ((((uint8_t *)&gm_l24) + 8) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t139, gm_srckg5f2c0p31ur, 440, 17);
                uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t138) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                uint8_t t141 = (uint8_t)(t140 == ((uint8_t)0ULL));
                if (!t141) {
                    goto gm_lb24;
                }
                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t138) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                gm_l25 = t142;
                {
                    {
                        {
                            void * t143 = gm_l0;
                            void * t144 = (void *)(((uint8_t *)t143) + 8);
                            gm_t88rcucg1b7uk t145 = gm_l25;
                            gm_f_kind_jcnt4hm((void *)&gm_l37, t144, GM_CONV(gm_csguankko4lmhd, t145));
                        }
                        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                        uint8_t t147 = (uint8_t)(t146 == ((uint8_t)19ULL));
                        if (!t147) {
                            goto gm_lb26;
                        }
                        uint32_t t148 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 8));
                        gm_l26 = t148;
                        void * t149 = gm_l3;
                        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)t149));
                        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                        uint32_t t152 = gm_l26;
                        gm_tkn42u7na3t81 t153 = gm_f_bound_to_5c11t0h(t151, t152);
                        gm_l39 = t153;
                        {
                            {
                                uint8_t t154 = (uint8_t)((((uint8_t *)&gm_l39))[4] != 0);
                                if (t154) {
                                    goto gm_lb28;
                                }
                                gm_l38 = ((uint8_t)0);
                                goto gm_lb27;
                            }
gm_lb28:;
                            {
                                gm_l38 = ((uint8_t)1);
                                goto gm_lb27;
                            }
gm_lb29:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 442, 8);
                        }
gm_lb27:;
                        uint8_t t155 = gm_l38;
                        if (t155) {
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        {
                            gm_trqfvmjtt4sij t156;
                            gm_f_list_084fcd6((void *)&t156, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l27 = t156;
                        }
                        void * t157 = gm_l2;
                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)t157));
                        gm_l28 = t158;
                        gm_l29 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t159 = gm_l29;
                                uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                uint8_t t161 = (t159 < t160);
                                if (!t161) {
                                    goto gm_lb30;
                                }
                                {
                                    void * t162 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                    uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                    uint64_t t164 = gm_l29;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l30), (((uint8_t *)t162) + (uintptr_t)t164 * 24), 24);
                                            gm_l31 = ((uint8_t)0);
                                            void * t165 = gm_l1;
                                            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)t165));
                                            gm_l32 = t166;
                                            gm_l33 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t167 = gm_l33;
                                                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                    uint8_t t169 = (t167 < t168);
                                                    if (!t169) {
                                                        goto gm_lb35;
                                                    }
                                                    {
                                                        void * t170 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                        uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                        uint64_t t172 = gm_l33;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&gm_l34), (((uint8_t *)t170) + (uintptr_t)t172 * 24), 24);
                                                                {
                                                                    {
                                                                        uint32_t t173 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                                                        uint8_t t174 = (!(t173 == ((uint32_t)0ULL)));
                                                                        if (!t174) {
                                                                            goto gm_lb41;
                                                                        }
                                                                        uint32_t t175 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                                                        uint32_t t176 = GM_LD(uint32_t, ((uint8_t *)&gm_l30));
                                                                        uint8_t t177 = ((t175 == t176));
                                                                        if (!t177) {
                                                                            goto gm_lb41;
                                                                        }
                                                                        gm_l31 = ((uint8_t)1);
                                                                        goto gm_lb40;
                                                                    }
gm_lb41:;
                                                                }
gm_lb40:;
                                                                goto gm_lb38;
                                                            }
gm_lb39:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 446, 9);
                                                        }
gm_lb38:;
                                                    }
gm_lb37:;
                                                    uint64_t t178 = gm_l33;
                                                    uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                                                    uint64_t t181 = r179;
                                                    gm_l33 = t181;
                                                }
                                            }
gm_lb35:;
                                            uint8_t t182 = gm_l31;
                                            uint8_t t183 = (uint8_t)!t182;
                                            if (t183) {
                                                void * t184 = (void *)((uint8_t *)&F.l27);
                                                gm_tftb1odc739t5 t185 = gm_f_push_d1r4h19(t184, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l30)));
                                                gm_l40 = t185;
                                                uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srckg5f2c0p31ur, 449, 24);
                                            }
                                            goto gm_lb33;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 444, 5);
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                uint64_t t187 = gm_l29;
                                uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                                uint64_t t190 = r188;
                                gm_l29 = t190;
                            }
                        }
gm_lb30:;
                        {
                            void * t191 = gm_l3;
                            uint32_t t192 = gm_l26;
                            void * t193 = gm_l0;
                            void * t194 = (void *)(((uint8_t *)t193) + 8);
                            gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&F.l27));
                            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
                            gm_tnpifv20irnph t197 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t196);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t53at80pfdus3 t198;
                            memset(&t198, 0, sizeof t198);
                            GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)24ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t198 + 8), t197);
                            gm_t88rcucg1b7uk t199 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t194, &t198);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tplpbqf3duvn3 t200;
                            GM_ST(uint32_t, ((uint8_t *)&t200 + 0), t192);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t200 + 4), t199);
                            gm_tftb1odc739t5 t201 = gm_f_push_h69n5fi(t191, GM_CONV(gm_cs7e9ppsd384bj, t200));
                            gm_l41 = t201;
                        }
                        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srckg5f2c0p31ur, 451, 5);
                        goto gm_lb25;
                    }
gm_lb26:;
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb25:;
                goto gm_lb23;
            }
gm_lb24:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb23:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_match_fn_params_7to7qbv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_match_fn_params_7to7qbv F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    void * gm_l2; /* g */
    void * gm_l3; /* binds */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* p */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tlcm4lvu3g1a8 gm_l8;
    void * t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    void * t9 = gm_l2;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint8_t t11 = (!(t8 == t10));
    if (t11) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t12 = gm_l1;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    gm_l4 = t13;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l5;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                uint64_t t17 = gm_l5;
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t20 = gm_l5;
                gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + (uintptr_t)t20 * 4));
                gm_tlcm4lvu3g1a8 t22;
                GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t17);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 8), t21);
                gm_l8 = t22;
                {
                    {
                        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                        gm_l6 = t23;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 8));
                        gm_l7 = t24;
                        void * t25 = gm_l0;
                        gm_t88rcucg1b7uk t26 = gm_l7;
                        void * t27 = gm_l2;
                        void * t28 = GM_LD(void *, ((uint8_t *)t27));
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
                        uint64_t t30 = gm_l6;
                        if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_srckg5f2c0p31ur, 399, 57);
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + (uintptr_t)t30 * 4));
                        void * t32 = gm_l3;
                        gm_b0_1 t33 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t25, t26, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 399, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l5;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l5 = t37;
        }
    }
gm_lb1:;
    void * t38 = gm_l0;
    void * t39 = gm_l1;
    gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 16));
    void * t41 = gm_l2;
    gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 16));
    void * t43 = gm_l3;
    gm_b0_1 t44 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t38, t40, t42, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t45 = gm_l0;
    void * t46 = gm_l1;
    void * t47 = (void *)(((uint8_t *)t46) + 24);
    void * t48 = gm_l2;
    void * t49 = (void *)(((uint8_t *)t48) + 24);
    void * t50 = gm_l3;
    gm_b0_1 t51 = gm_f_match_effects_0l1vbuc((void *)NULL, cx, NULL, t45, t47, t49, t50);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_match_params_6vndkud F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* pat */
    gm_t88rcucg1b7uk gm_l2; /* actual */
    void * gm_l3; /* binds */
    void * gm_l4; /* tys */
    uint32_t gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* x */
    gm_t88rcucg1b7uk gm_l9; /* len2 */
    gm_t88rcucg1b7uk gm_l10; /* y */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* y */
    uint32_t gm_l13; /* p */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_t88rcucg1b7uk gm_l18; /* v */
    gm_t88rcucg1b7uk gm_l19; /* x */
    gm_t88rcucg1b7uk gm_l20; /* y */
    gm_t88rcucg1b7uk gm_l21; /* x */
    gm_t88rcucg1b7uk gm_l22; /* y */
    gm_t88rcucg1b7uk gm_l23; /* x */
    gm_t88rcucg1b7uk gm_l24; /* y */
    gm_t88rcucg1b7uk gm_l25; /* x */
    gm_t88rcucg1b7uk gm_l26; /* y */
    gm_t88rcucg1b7uk gm_l27; /* x */
    gm_t88rcucg1b7uk gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* e1 */
    gm_t88rcucg1b7uk gm_l30; /* o1 */
    gm_t88rcucg1b7uk gm_l31; /* e2 */
    gm_t88rcucg1b7uk gm_l32; /* o2 */
    uint32_t gm_l33; /* x */
    gm_b16_8 gm_l34; /* xs */
    uint32_t gm_l35; /* y */
    gm_b16_8 gm_l36; /* ys */
    uint8_t gm_l37; /* same */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    gm_t88rcucg1b7uk gm_l42; /* pt */
    gm_t88rcucg1b7uk gm_l43; /* qt */
    gm_b16_8 gm_l44; /* xs */
    gm_b16_8 gm_l45; /* ys */
    gm_b16_8 gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    gm_t88rcucg1b7uk gm_l49; /* x */
    gm_tnpifv20irnph gm_l55; /* x */
    gm_tnpifv20irnph gm_l56; /* y */
    GM_STACK_CHECK(&F, 2095u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l57;
    gm_t53at80pfdus3 gm_l58;
    uint8_t gm_l59;
    gm_tkn42u7na3t81 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_t53at80pfdus3 gm_l62;
    uint8_t gm_l63;
    gm_tkn42u7na3t81 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    uint8_t gm_l67;
    gm_tdr8bh2bm3fc8 gm_l68;
    gm_tlcm4lvu3g1a8 gm_l69;
    {
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 8);
        gm_l4 = t75;
        void * t76 = gm_l4;
        gm_t88rcucg1b7uk t77 = gm_l2;
        uint8_t t78 = gm_f_has_infer_3o82a0b(t76, GM_CONV(gm_csguankko4lmhd, t77));
        if (t78) {
            gm_l57 = ((uint8_t)1);
        } else {
            void * t79 = gm_l4;
            gm_t88rcucg1b7uk t80 = gm_l2;
            uint8_t t81 = gm_f_is_error_8k27gd1(t79, GM_CONV(gm_csguankko4lmhd, t80));
            gm_l57 = t81;
        }
        uint8_t t82 = gm_l57;
        if (t82) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        void * t83 = gm_l4;
        gm_t88rcucg1b7uk t84 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l5, t83, GM_CONV(gm_csguankko4lmhd, t84));
        void * t85 = gm_l4;
        gm_t88rcucg1b7uk t86 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l58, t85, GM_CONV(gm_csguankko4lmhd, t86));
        {
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)19ULL));
                    if (!t88) {
                        goto gm_lb2;
                    }
                    uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&gm_l58) + 8));
                    gm_l6 = t89;
                    void * t90 = gm_l3;
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)t90));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                    uint32_t t93 = gm_l6;
                    gm_tkn42u7na3t81 t94 = gm_f_bound_to_5c11t0h(t92, t93);
                    gm_l60 = t94;
                    {
                        {
                            uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l60))[4] != 0);
                            if (t95) {
                                goto gm_lb4;
                            }
                            gm_l59 = ((uint8_t)1);
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            gm_l59 = ((uint8_t)0);
                            goto gm_lb3;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 349, 25);
                    }
gm_lb3:;
                    uint8_t t96 = gm_l59;
                    if (t96) {
                        void * t97 = gm_l3;
                        uint32_t t98 = gm_l6;
                        gm_t88rcucg1b7uk t99 = gm_l2;
                        gm_tplpbqf3duvn3 t100;
                        GM_ST(uint32_t, ((uint8_t *)&t100 + 0), t98);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t100 + 4), t99);
                        gm_tftb1odc739t5 t101 = gm_f_push_h69n5fi(t97, GM_CONV(gm_cs7e9ppsd384bj, t100));
                        gm_l61 = t101;
                        uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                        if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srckg5f2c0p31ur, 349, 60);
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)20ULL));
                    if (!t104) {
                        goto gm_lb6;
                    }
                    gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l58) + 8));
                    gm_l7 = t105;
                    gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l8 = t106;
                    {
                        {
                            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)20ULL));
                            if (!t108) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l9 = t109;
                            gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
                            gm_l10 = t110;
                            void * t111 = gm_l0;
                            gm_t88rcucg1b7uk t112 = gm_l7;
                            gm_t88rcucg1b7uk t113 = gm_l9;
                            void * t114 = gm_l3;
                            gm_b0_1 t115 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t111, t112, t113, t114);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t116 = gm_l0;
                            gm_t88rcucg1b7uk t117 = gm_l8;
                            gm_t88rcucg1b7uk t118 = gm_l10;
                            void * t119 = gm_l3;
                            gm_b0_1 t120 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t116, t117, t118, t119);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t122 = (uint8_t)(t121 == ((uint8_t)11ULL));
                            if (!t122) {
                                goto gm_lb9;
                            }
                            uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                            gm_l11 = t123;
                            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 8));
                            gm_l12 = t124;
                            uint8_t t125 = GM_LD(uint8_t, (((((uint8_t *)&F.l5) + 8) + 12) + 0));
                            uint8_t t126 = (uint8_t)(t125 == ((uint8_t)0ULL));
                            if (!t126) {
                                goto gm_lb9;
                            }
                            {
                                {
                                    {
                                        void * t127 = gm_l4;
                                        gm_t88rcucg1b7uk t128 = gm_l7;
                                        gm_f_kind_jcnt4hm((void *)&gm_l62, t127, GM_CONV(gm_csguankko4lmhd, t128));
                                    }
                                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)19ULL));
                                    if (!t130) {
                                        goto gm_lb11;
                                    }
                                    uint32_t t131 = GM_LD(uint32_t, (((uint8_t *)&gm_l62) + 8));
                                    gm_l13 = t131;
                                    void * t132 = gm_l3;
                                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)t132));
                                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                                    uint32_t t135 = gm_l13;
                                    gm_tkn42u7na3t81 t136 = gm_f_bound_to_5c11t0h(t134, t135);
                                    gm_l64 = t136;
                                    {
                                        {
                                            uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l64))[4] != 0);
                                            if (t137) {
                                                goto gm_lb13;
                                            }
                                            gm_l63 = ((uint8_t)1);
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        {
                                            gm_l63 = ((uint8_t)0);
                                            goto gm_lb12;
                                        }
gm_lb14:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 357, 51);
                                    }
gm_lb12:;
                                    uint8_t t138 = gm_l63;
                                    if (!t138) {
                                        goto gm_lb11;
                                    }
                                    {
                                        gm_trqfvmjtt4sij t139;
                                        gm_f_list_uvfu1k1((void *)&t139, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l14 = t139;
                                    }
                                    gm_l16 = ((uint64_t)0ULL);
                                    gm_l15 = ((uint64_t)8ULL);
                                    {
                                        for (;;) {
                                            uint64_t t140 = gm_l16;
                                            uint64_t t141 = gm_l15;
                                            uint8_t t142 = (t140 < t141);
                                            if (!t142) {
                                                goto gm_lb15;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t143 = gm_l16;
                                                        gm_l17 = t143;
                                                        void * t144 = (void *)((uint8_t *)&F.l14);
                                                        uint64_t t145 = gm_l11;
                                                        uint64_t t146 = gm_l17;
                                                        uint64_t r147; bool o148 = __builtin_mul_overflow(t146, ((uint64_t)8ULL), &r147);
                                                        if (o148) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 359, 58);
                                                        uint64_t t149 = r147;
                                                        unsigned __int128 n150 = (unsigned __int128)t149;
                                                        if (n150 >= 64) gm_trap_shift((uint64_t)n150, "u64", gm_srckg5f2c0p31ur, 359, 49);
                                                        uint64_t t151 = (uint64_t)(t145 >> (int)n150);
                                                        uint8_t t152 = (uint8_t)t151;
                                                        gm_tftb1odc739t5 t153 = gm_f_push_t83rg2m(t144, t152);
                                                        gm_l65 = t153;
                                                        uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                        if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srckg5f2c0p31ur, 359, 37);
                                                        goto gm_lb18;
                                                    }
gm_lb19:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 359, 21);
                                                }
gm_lb18:;
                                            }
gm_lb17:;
                                            uint64_t t155 = gm_l16;
                                            uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                                            uint64_t t158 = r156;
                                            gm_l16 = t158;
                                        }
                                    }
gm_lb15:;
                                    {
                                        void * t159 = gm_l4;
                                        void * t160 = gm_l4;
                                        gm_t88rcucg1b7uk t161 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t160, ((uint8_t)0), ((uint16_t)64ULL));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                                        gm_tcmkompbl09il t164;
                                        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t164 + 0), t161);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t164 + 8), t163);
                                        gm_t428ue2u3hf5g t165;
                                        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t165 + 8), t164);
                                        gm_t53at80pfdus3 t166;
                                        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t166 + 40, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t166 + 48, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)21ULL));
                                        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t166 + 8), t165);
                                        gm_t88rcucg1b7uk t167 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t159, &t166);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l18 = t167;
                                    }
                                    void * t168 = gm_l3;
                                    uint32_t t169 = gm_l13;
                                    gm_t88rcucg1b7uk t170 = gm_l18;
                                    gm_tplpbqf3duvn3 t171;
                                    GM_ST(uint32_t, ((uint8_t *)&t171 + 0), t169);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t171 + 4), t170);
                                    gm_tftb1odc739t5 t172 = gm_f_push_h69n5fi(t168, GM_CONV(gm_cs7e9ppsd384bj, t171));
                                    gm_l66 = t172;
                                    uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                    if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srckg5f2c0p31ur, 361, 21);
                                    goto gm_lb10;
                                }
gm_lb11:;
                            }
gm_lb10:;
                            void * t174 = gm_l0;
                            gm_t88rcucg1b7uk t175 = gm_l8;
                            gm_t88rcucg1b7uk t176 = gm_l12;
                            void * t177 = gm_l3;
                            gm_b0_1 t178 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t174, t175, t176, t177);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            goto gm_lb7;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 350, 32);
                    }
gm_lb7:;
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)8ULL));
                    if (!t180) {
                        goto gm_lb21;
                    }
                    gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l19 = t181;
                    {
                        {
                            uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t183 = (uint8_t)(t182 == ((uint8_t)8ULL));
                            if (!t183) {
                                goto gm_lb23;
                            }
                            gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
                            gm_l20 = t184;
                            void * t185 = gm_l0;
                            gm_t88rcucg1b7uk t186 = gm_l19;
                            gm_t88rcucg1b7uk t187 = gm_l20;
                            void * t188 = gm_l3;
                            gm_b0_1 t189 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t185, t186, t187, t188);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                    }
gm_lb22:;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)10ULL));
                    if (!t191) {
                        goto gm_lb24;
                    }
                    gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l21 = t192;
                    {
                        {
                            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t194 = (uint8_t)(t193 == ((uint8_t)10ULL));
                            if (!t194) {
                                goto gm_lb26;
                            }
                            gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
                            gm_l22 = t195;
                            void * t196 = gm_l0;
                            gm_t88rcucg1b7uk t197 = gm_l21;
                            gm_t88rcucg1b7uk t198 = gm_l22;
                            void * t199 = gm_l3;
                            gm_b0_1 t200 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t196, t197, t198, t199);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                    }
gm_lb25:;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t202 = (uint8_t)(t201 == ((uint8_t)9ULL));
                    if (!t202) {
                        goto gm_lb27;
                    }
                    gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l23 = t203;
                    {
                        {
                            uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t205 = (uint8_t)(t204 == ((uint8_t)9ULL));
                            if (!t205) {
                                goto gm_lb29;
                            }
                            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
                            gm_l24 = t206;
                            void * t207 = gm_l0;
                            gm_t88rcucg1b7uk t208 = gm_l23;
                            gm_t88rcucg1b7uk t209 = gm_l24;
                            void * t210 = gm_l3;
                            gm_b0_1 t211 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t207, t208, t209, t210);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb28;
                        }
gm_lb29:;
                    }
gm_lb28:;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)11ULL));
                    if (!t213) {
                        goto gm_lb30;
                    }
                    gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 8));
                    gm_l25 = t214;
                    {
                        {
                            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t216 = (uint8_t)(t215 == ((uint8_t)11ULL));
                            if (!t216) {
                                goto gm_lb32;
                            }
                            gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 8));
                            gm_l26 = t217;
                            void * t218 = gm_l0;
                            gm_t88rcucg1b7uk t219 = gm_l25;
                            gm_t88rcucg1b7uk t220 = gm_l26;
                            void * t221 = gm_l3;
                            gm_b0_1 t222 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t218, t219, t220, t221);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb31;
                        }
gm_lb32:;
                    }
gm_lb31:;
                    goto gm_lb1;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t224 = (uint8_t)(t223 == ((uint8_t)13ULL));
                    if (!t224) {
                        goto gm_lb33;
                    }
                    gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l58) + 8));
                    gm_l27 = t225;
                    {
                        {
                            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t227 = (uint8_t)(t226 == ((uint8_t)13ULL));
                            if (!t227) {
                                goto gm_lb35;
                            }
                            gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l28 = t228;
                            void * t229 = gm_l0;
                            gm_t88rcucg1b7uk t230 = gm_l27;
                            gm_t88rcucg1b7uk t231 = gm_l28;
                            void * t232 = gm_l3;
                            gm_b0_1 t233 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t229, t230, t231, t232);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb34;
                        }
gm_lb35:;
                    }
gm_lb34:;
                    goto gm_lb1;
                }
gm_lb33:;
                {
                    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t235 = (uint8_t)(t234 == ((uint8_t)14ULL));
                    if (!t235) {
                        goto gm_lb36;
                    }
                    gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l58) + 8));
                    gm_l29 = t236;
                    gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l30 = t237;
                    {
                        {
                            uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t239 = (uint8_t)(t238 == ((uint8_t)14ULL));
                            if (!t239) {
                                goto gm_lb38;
                            }
                            gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l31 = t240;
                            gm_t88rcucg1b7uk t241 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
                            gm_l32 = t241;
                            void * t242 = gm_l0;
                            gm_t88rcucg1b7uk t243 = gm_l29;
                            gm_t88rcucg1b7uk t244 = gm_l31;
                            void * t245 = gm_l3;
                            gm_b0_1 t246 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t242, t243, t244, t245);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t247 = gm_l0;
                            gm_t88rcucg1b7uk t248 = gm_l30;
                            gm_t88rcucg1b7uk t249 = gm_l32;
                            void * t250 = gm_l3;
                            gm_b0_1 t251 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t247, t248, t249, t250);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                    }
gm_lb37:;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t253 = (uint8_t)(t252 == ((uint8_t)17ULL));
                    if (!t253) {
                        goto gm_lb39;
                    }
                    uint32_t t254 = GM_LD(uint32_t, (((uint8_t *)&gm_l58) + 8));
                    gm_l33 = t254;
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l58) + 8) + 8));
                    gm_l34 = t255;
                    {
                        {
                            uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t257 = (uint8_t)(t256 == ((uint8_t)17ULL));
                            if (!t257) {
                                goto gm_lb41;
                            }
                            uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
                            gm_l35 = t258;
                            gm_b16_8 t259 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 8) + 8));
                            gm_l36 = t259;
                            void * t260 = gm_l4;
                            uint32_t t261 = gm_l33;
                            uint32_t t262 = gm_f_template_9o92cvv(t260, t261);
                            void * t263 = gm_l4;
                            uint32_t t264 = gm_l35;
                            uint32_t t265 = gm_f_template_9o92cvv(t263, t264);
                            uint8_t t266 = ((t262 == t265));
                            if (t266) {
                                gm_l67 = ((uint8_t)1);
                            } else {
                                void * t267 = gm_l4;
                                uint32_t t268 = gm_l33;
                                uint32_t t269 = gm_l35;
                                uint8_t t270 = gm_f_soa_const_view_od97rln(t267, t268, t269);
                                gm_l67 = t270;
                            }
                            uint8_t t271 = gm_l67;
                            gm_l37 = t271;
                            {
                                {
                                    uint8_t t272 = gm_l37;
                                    if (!t272) {
                                        goto gm_lb43;
                                    }
                                    uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                    uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                    uint8_t t275 = ((t273 == t274));
                                    if (!t275) {
                                        goto gm_lb43;
                                    }
                                    gm_b16_8 t276 = gm_l34;
                                    gm_l38 = t276;
                                    gm_l39 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t277 = gm_l39;
                                            uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint8_t t279 = (t277 < t278);
                                            if (!t279) {
                                                goto gm_lb44;
                                            }
                                            {
                                                {
                                                    uint64_t t280 = gm_l39;
                                                    void * t281 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                    uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                    uint64_t t283 = gm_l39;
                                                    gm_toa2vhe3l4hfr t284 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t281) + (uintptr_t)t283 * 40));
                                                    GM_ST(uint64_t, ((uint8_t *)&gm_l68 + 0), t280);
                                                    GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&gm_l68 + 8), t284);
                                                }
                                                {
                                                    {
                                                        uint64_t t285 = GM_LD(uint64_t, ((uint8_t *)&gm_l68));
                                                        gm_l40 = t285;
                                                        memmove(((uint8_t *)&F.l41), (((uint8_t *)&gm_l68) + 8), 40);
                                                        {
                                                            {
                                                                uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&F.l41) + 0));
                                                                uint8_t t287 = (uint8_t)(t286 == ((uint8_t)0ULL));
                                                                if (!t287) {
                                                                    goto gm_lb50;
                                                                }
                                                                gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l41) + 8));
                                                                gm_l42 = t288;
                                                                void * t289 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                                uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                                uint64_t t291 = gm_l40;
                                                                if ((uintptr_t)t291 >= (uintptr_t)(t290)) gm_trap_index((uintptr_t)t291, t290, gm_srckg5f2c0p31ur, 380, 56);
                                                                uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t289) + (uintptr_t)t291 * 40) + 0));
                                                                uint8_t t293 = (uint8_t)(t292 == ((uint8_t)0ULL));
                                                                if (!t293) {
                                                                    goto gm_lb50;
                                                                }
                                                                gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t289) + (uintptr_t)t291 * 40) + 8));
                                                                gm_l43 = t294;
                                                                void * t295 = gm_l0;
                                                                gm_t88rcucg1b7uk t296 = gm_l42;
                                                                gm_t88rcucg1b7uk t297 = gm_l43;
                                                                void * t298 = gm_l3;
                                                                gm_b0_1 t299 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t295, t296, t297, t298);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                goto gm_lb49;
                                                            }
gm_lb50:;
                                                        }
gm_lb49:;
                                                        goto gm_lb47;
                                                    }
gm_lb48:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 379, 17);
                                                }
gm_lb47:;
                                            }
gm_lb46:;
                                            uint64_t t300 = gm_l39;
                                            uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                                            uint64_t t303 = r301;
                                            gm_l39 = t303;
                                        }
                                    }
gm_lb44:;
                                    goto gm_lb42;
                                }
gm_lb43:;
                            }
gm_lb42:;
                            goto gm_lb40;
                        }
gm_lb41:;
                    }
gm_lb40:;
                    goto gm_lb1;
                }
gm_lb39:;
            }
            {
                {
                    uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t305 = (uint8_t)(t304 == ((uint8_t)12ULL));
                    if (!t305) {
                        goto gm_lb51;
                    }
                    gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                    gm_l44 = t306;
                    {
                        {
                            uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t308 = (uint8_t)(t307 == ((uint8_t)12ULL));
                            if (!t308) {
                                goto gm_lb53;
                            }
                            gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                            gm_l45 = t309;
                            uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                            uint8_t t312 = ((t310 == t311));
                            if (!t312) {
                                goto gm_lb53;
                            }
                            gm_b16_8 t313 = gm_l44;
                            gm_l46 = t313;
                            gm_l47 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t314 = gm_l47;
                                    uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                    uint8_t t316 = (t314 < t315);
                                    if (!t316) {
                                        goto gm_lb54;
                                    }
                                    {
                                        uint64_t t317 = gm_l47;
                                        void * t318 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                        uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                        uint64_t t320 = gm_l47;
                                        gm_t88rcucg1b7uk t321 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t318) + (uintptr_t)t320 * 4));
                                        gm_tlcm4lvu3g1a8 t322;
                                        GM_ST(uint32_t, (uint8_t *)&t322 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t322 + 0), t317);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t322 + 8), t321);
                                        gm_l69 = t322;
                                        {
                                            {
                                                uint64_t t323 = GM_LD(uint64_t, ((uint8_t *)&gm_l69));
                                                gm_l48 = t323;
                                                gm_t88rcucg1b7uk t324 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l69) + 8));
                                                gm_l49 = t324;
                                                void * t325 = gm_l0;
                                                gm_t88rcucg1b7uk t326 = gm_l49;
                                                void * t327 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                                uint64_t t328 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                uint64_t t329 = gm_l48;
                                                if ((uintptr_t)t329 >= (uintptr_t)(t328)) gm_trap_index((uintptr_t)t329, t328, gm_srckg5f2c0p31ur, 385, 59);
                                                gm_t88rcucg1b7uk t330 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t327) + (uintptr_t)t329 * 4));
                                                void * t331 = gm_l3;
                                                gm_b0_1 t332 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t325, t326, t330, t331);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                goto gm_lb57;
                                            }
gm_lb58:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 385, 13);
                                        }
gm_lb57:;
                                    }
gm_lb56:;
                                    uint64_t t333 = gm_l47;
                                    uint64_t r334; bool o335 = __builtin_add_overflow(t333, ((uint64_t)1ULL), &r334);
                                    uint64_t t336 = r334;
                                    gm_l47 = t336;
                                }
                            }
gm_lb54:;
                            goto gm_lb52;
                        }
gm_lb53:;
                    }
gm_lb52:;
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t338 = (uint8_t)(t337 == ((uint8_t)15ULL));
                    if (!t338) {
                        goto gm_lb59;
                    }
                    memmove(((uint8_t *)&F.l50), (((uint8_t *)&gm_l58) + 8), 48);
                    {
                        {
                            {
                                {
                                    uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                    uint8_t t340 = (uint8_t)(t339 == ((uint8_t)15ULL));
                                    if (!t340) {
                                        goto gm_lb63;
                                    }
                                    memmove(((uint8_t *)&F.l51), (((uint8_t *)&F.l5) + 8), 48);
                                    goto gm_lb62;
                                }
gm_lb63:;
                                {
                                    uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                    uint8_t t342 = (uint8_t)(t341 == ((uint8_t)16ULL));
                                    if (!t342) {
                                        goto gm_lb64;
                                    }
                                    memmove(((uint8_t *)&F.l51), (((uint8_t *)&F.l5) + 8), 48);
                                    goto gm_lb62;
                                }
gm_lb64:;
                                goto gm_lb61;
                            }
gm_lb62:;
                            void * t343 = gm_l0;
                            void * t344 = (void *)((uint8_t *)&F.l50);
                            void * t345 = (void *)((uint8_t *)&F.l51);
                            void * t346 = gm_l3;
                            gm_b0_1 t347 = gm_f_match_fn_params_7to7qbv((void *)NULL, cx, NULL, t343, t344, t345, t346);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb60;
                        }
gm_lb61:;
                        {
                            goto gm_lb60;
                        }
gm_lb65:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 387, 19);
                    }
gm_lb60:;
                    goto gm_lb1;
                }
gm_lb59:;
            }
            {
                {
                    uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t349 = (uint8_t)(t348 == ((uint8_t)16ULL));
                    if (!t349) {
                        goto gm_lb66;
                    }
                    memmove(((uint8_t *)&F.l53), (((uint8_t *)&gm_l58) + 8), 48);
                    {
                        {
                            uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t351 = (uint8_t)(t350 == ((uint8_t)16ULL));
                            if (!t351) {
                                goto gm_lb68;
                            }
                            memmove(((uint8_t *)&F.l54), (((uint8_t *)&F.l5) + 8), 48);
                            void * t352 = gm_l0;
                            void * t353 = (void *)((uint8_t *)&F.l53);
                            void * t354 = (void *)((uint8_t *)&F.l54);
                            void * t355 = gm_l3;
                            gm_b0_1 t356 = gm_f_match_fn_params_7to7qbv((void *)NULL, cx, NULL, t352, t353, t354, t355);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb67;
                        }
gm_lb68:;
                    }
gm_lb67:;
                    goto gm_lb1;
                }
gm_lb66:;
                {
                    uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t358 = (uint8_t)(t357 == ((uint8_t)24ULL));
                    if (!t358) {
                        goto gm_lb69;
                    }
                    gm_tnpifv20irnph t359 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l58) + 8));
                    gm_l55 = t359;
                    {
                        {
                            uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t361 = (uint8_t)(t360 == ((uint8_t)24ULL));
                            if (!t361) {
                                goto gm_lb71;
                            }
                            gm_tnpifv20irnph t362 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l5) + 8));
                            gm_l56 = t362;
                            void * t363 = gm_l0;
                            void * t364 = (void *)((uint8_t *)&gm_l55);
                            void * t365 = (void *)((uint8_t *)&gm_l56);
                            void * t366 = gm_l3;
                            gm_b0_1 t367 = gm_f_match_effects_0l1vbuc((void *)NULL, cx, NULL, t363, t364, t365, t366);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb70;
                        }
gm_lb71:;
                    }
gm_lb70:;
                    goto gm_lb1;
                }
gm_lb69:;
                {
                    goto gm_lb1;
                }
gm_lb72:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 348, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_not_first_class_dve75oi F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* part */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    uint8_t t4 = (gm_eq9m6mvtmgde(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_b16_8 t7 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
        gm_f_format_ii7qb9k((void *)&gm_l3, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srckg5f2c0p31ur, 194, 27);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        F.ret = t11;
        goto gm_fret;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l1;
        gm_b16_8 t14 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_b16_8 t17 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t18;
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 16), t17);
        gm_f_format_9h7i0ht((void *)&gm_l4, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srckg5f2c0p31ur, 195, 12);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_has_2r09oc7 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l5; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 1128));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1128) + 8));
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    if ((uintptr_t)t11 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t11, t9, gm_srckg5f2c0p31ur, 44, 14);
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + (uintptr_t)t11 * 48) + 8));
    gm_l3 = t12;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l4;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t18 = gm_l4;
                {
                    {
                        gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
                        gm_l5 = t19;
                        gm_t9qkh0rsooc8d t20 = gm_l5;
                        gm_t9qkh0rsooc8d t21 = gm_l2;
                        uint8_t t22 = gm_f_implies_ibnta05(t20, t21);
                        if (t22) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 44, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l4;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l4 = t26;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_constraint_6f1aotv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_constraint_6f1aotv F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 278u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            gm_b16_8 t11 = gm_l0;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sqri8tum3tpfm));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
            uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
            if (!t13) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t15;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[1] = 1;
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t16 = gm_l0;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s7kt2bei71pgo));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
            uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (!t18) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t20;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[1] = 1;
            gm_l1 = t20;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t21 = gm_l0;
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_snoj0t5lf2lht));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
            uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
            if (!t23) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t25;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[1] = 1;
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t26 = gm_l0;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_seggohi50ng09));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(3));
            uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
            if (!t28) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)3ULL));
            gm_taari1s0m51a7 t30;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30), t29);
            ((uint8_t *)&t30)[1] = 1;
            gm_l1 = t30;
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_b16_8 t31 = gm_l0;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_se19nf1421e37));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
            if (!t33) {
                goto gm_lb6;
            }
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)4ULL));
            gm_taari1s0m51a7 t35;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35), t34);
            ((uint8_t *)&t35)[1] = 1;
            gm_l1 = t35;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_b16_8 t36 = gm_l0;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s7s25m3nlrif2));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(5));
            uint8_t t38 = (gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
            if (!t38) {
                goto gm_lb7;
            }
            gm_t9qkh0rsooc8d t39;
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)5ULL));
            gm_taari1s0m51a7 t40;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t40), t39);
            ((uint8_t *)&t40)[1] = 1;
            gm_l1 = t40;
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_b16_8 t41 = gm_l0;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sfv26hkjngt1i));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(5));
            uint8_t t43 = (gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
            if (!t43) {
                goto gm_lb8;
            }
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)6ULL));
            gm_taari1s0m51a7 t45;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[1] = 1;
            gm_l1 = t45;
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_taari1s0m51a7 t46;
            memset(&t46, 0, sizeof t46);
            gm_l1 = t46;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 3, 56);
    }
gm_lb1:;
    gm_taari1s0m51a7 t47 = gm_l1;
    F.ret = t47;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_fields_i69c54j F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l5; /* ok */
    GM_STACK_CHECK(&F, 332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_tkn42u7na3t81 gm_l11;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_l2 = t13;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l3;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t19 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t17) + (uintptr_t)t19 * 40), 40);
                        {
                            {
                                uint8_t t20 = (uint8_t)(((((uint8_t *)a1) + 40))[4] != 0);
                                if (t20) {
                                    goto gm_lb7;
                                }
                                gm_l6 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l6 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 175, 12);
                        }
gm_lb6:;
                        uint8_t t21 = gm_l6;
                        if (t21) {
                            void * t22 = gm_l0;
                            gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                            uint8_t t24 = gm_f_is_plain_n60iihm(t22, GM_CONV(gm_csguankko4lmhd, t23));
                            uint8_t t25 = (uint8_t)!t24;
                            if (t25) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                        } else {
                            void * t26 = gm_l0;
                            void * t27 = (void *)(((uint8_t *)t26) + 8);
                            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                            uint8_t t29 = gm_f_is_integer_8d4f9ek(t27, GM_CONV(gm_csguankko4lmhd, t28));
                            if (t29) {
                                gm_l8 = ((uint8_t)1);
                            } else {
                                void * t30 = gm_l0;
                                void * t31 = (void *)(((uint8_t *)t30) + 8);
                                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                gm_t53at80pfdus3 t33;
                                gm_f_kind_jcnt4hm((void *)&t33, t31, GM_CONV(gm_csguankko4lmhd, t32));
                                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                                uint8_t t35 = (uint8_t)((t34 == ((uint8_t)4ULL)));
                                gm_t53at80pfdus3 t36;
                                memset(&t36, 0, sizeof t36);
                                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)4ULL));
                                gm_l8 = t35;
                            }
                            uint8_t t37 = gm_l8;
                            if (t37) {
                                gm_l7 = ((uint8_t)1);
                            } else {
                                void * t38 = gm_l0;
                                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                gm_tkn42u7na3t81 t40 = gm_f_bits_struct_ty_l1kjtap(t38, GM_CONV(gm_csguankko4lmhd, t39));
                                gm_l11 = t40;
                                {
                                    {
                                        uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l11))[4] != 0);
                                        if (t41) {
                                            goto gm_lb10;
                                        }
                                        gm_l10 = ((uint8_t)0);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    {
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb9;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 179, 75);
                                }
gm_lb9:;
                                uint8_t t42 = gm_l10;
                                if (t42) {
                                    void * t43 = gm_l0;
                                    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                    uint8_t t45 = gm_f_is_plain_n60iihm(t43, GM_CONV(gm_csguankko4lmhd, t44));
                                    gm_l9 = t45;
                                } else {
                                    gm_l9 = ((uint8_t)0);
                                }
                                uint8_t t46 = gm_l9;
                                gm_l7 = t46;
                            }
                            uint8_t t47 = gm_l7;
                            gm_l5 = t47;
                            uint8_t t48 = gm_l5;
                            uint8_t t49 = (uint8_t)!t48;
                            if (t49) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 174, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t50 = gm_l3;
            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
            uint64_t t53 = r51;
            gm_l3 = t53;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j_c(void * a0, gm_tpautr4br7h3l a1) { return gm_f_plain_fields_i69c54j(a0, &a1); }
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_satisfies_p0n2er8 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    uint8_t gm_l4; /* is_float */
    GM_STACK_CHECK(&F, 319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    gm_t53at80pfdus3 gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    {
        {
            void * t15 = gm_l0;
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)19ULL));
            if (!t19) {
                goto gm_lb2;
            }
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t20;
            void * t21 = gm_l0;
            uint32_t t22 = gm_l3;
            gm_t9qkh0rsooc8d t23 = gm_l2;
            uint8_t t24 = gm_f_param_has_2r09oc7(t21, t22, t23);
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t25 = gm_l0;
    void * t26 = (void *)(((uint8_t *)t25) + 8);
    gm_t88rcucg1b7uk t27 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t26, GM_CONV(gm_csguankko4lmhd, t27));
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)3ULL));
            if (!t29) {
                goto gm_lb4;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb3;
        }
gm_lb4:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb3;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 69, 17);
    }
gm_lb3:;
    {
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)0ULL));
            if (!t31) {
                goto gm_lb7;
            }
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l1;
            uint8_t t34 = gm_f_has_eq_iba53hs((void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t34;
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
            if (!t36) {
                goto gm_lb8;
            }
            void * t37 = gm_l0;
            gm_t88rcucg1b7uk t38 = gm_l1;
            uint8_t t39 = gm_f_has_ord_1io9knm(t37, GM_CONV(gm_csguankko4lmhd, t38));
            gm_l7 = t39;
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
            if (!t41) {
                goto gm_lb9;
            }
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 8);
            gm_t88rcucg1b7uk t44 = gm_l1;
            uint8_t t45 = gm_f_is_integer_8d4f9ek(t43, GM_CONV(gm_csguankko4lmhd, t44));
            if (t45) {
                gm_l8 = ((uint8_t)1);
            } else {
                uint8_t t46 = gm_l4;
                gm_l8 = t46;
            }
            uint8_t t47 = gm_l8;
            gm_l7 = t47;
            goto gm_lb6;
        }
gm_lb9:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)3ULL));
            if (!t49) {
                goto gm_lb10;
            }
            void * t50 = gm_l0;
            void * t51 = (void *)(((uint8_t *)t50) + 8);
            gm_t88rcucg1b7uk t52 = gm_l1;
            uint8_t t53 = gm_f_is_integer_8d4f9ek(t51, GM_CONV(gm_csguankko4lmhd, t52));
            gm_l7 = t53;
            goto gm_lb6;
        }
gm_lb10:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)4ULL));
            if (!t55) {
                goto gm_lb11;
            }
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 8);
            gm_t88rcucg1b7uk t58 = gm_l1;
            uint8_t t59 = gm_f_is_signed_integer_s66djm7(t57, GM_CONV(gm_csguankko4lmhd, t58));
            if (t59) {
                gm_l9 = ((uint8_t)1);
            } else {
                uint8_t t60 = gm_l4;
                gm_l9 = t60;
            }
            uint8_t t61 = gm_l9;
            gm_l7 = t61;
            goto gm_lb6;
        }
gm_lb11:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)5ULL));
            if (!t63) {
                goto gm_lb12;
            }
            uint8_t t64 = gm_l4;
            gm_l7 = t64;
            goto gm_lb6;
        }
gm_lb12:;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)6ULL));
            if (!t66) {
                goto gm_lb13;
            }
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l1;
            uint8_t t69 = gm_f_is_plain_n60iihm(t67, GM_CONV(gm_csguankko4lmhd, t68));
            gm_l7 = t69;
            goto gm_lb6;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 70, 12);
    }
gm_lb6:;
    uint8_t t70 = gm_l7;
    F.ret = t70;
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
inline __attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_scope_type_params_2di4dm5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* params */
    gm_t66j1im9ip128 gm_l2; /* before */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l6; /* pid */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        void * t10 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l2, t10);
    }
    gm_b16_8 t11 = gm_l1;
    gm_l3 = t11;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l4;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t17 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t15) + (uintptr_t)t17 * 56), 56);
                        uint8_t t18 = (uint8_t)(((((uint8_t *)&F.l5) + 40))[4] != 0);
                        if (t18) {
                            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 40));
                            gm_l6 = t19;
                        } else {
                            goto gm_lb3;
                        }
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1128));
                        uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1128) + 8));
                        uint32_t t23 = gm_l6;
                        uint64_t t24 = (uint64_t)t23;
                        if ((uintptr_t)t24 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t24, t22, gm_srckg5f2c0p31ur, 235, 12);
                        {
                            {
                                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t21) + (uintptr_t)t24 * 48) + 0));
                                uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                gm_l7 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l7 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 235, 12);
                        }
gm_lb6:;
                        uint8_t t27 = gm_l7;
                        if (t27) {
                            void * t28 = gm_l0;
                            void * t29 = (void *)(((uint8_t *)t28) + 1648);
                            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            uint32_t t31 = gm_l6;
                            gm_tommf4j9upomc t32;
                            GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
                            GM_ST(uint32_t, ((uint8_t *)&t32 + 16), t31);
                            gm_tftb1odc739t5 t33 = gm_f_push_sp4si0f(t29, &t32);
                            gm_l8 = t33;
                            uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                            if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckg5f2c0p31ur, 236, 13);
                        } else {
                            void * t35 = gm_l0;
                            void * t36 = (void *)(((uint8_t *)t35) + 1608);
                            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            void * t38 = gm_l0;
                            void * t39 = (void *)(((uint8_t *)t38) + 8);
                            uint32_t t40 = gm_l6;
                            gm_t88rcucg1b7uk t41 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tep2drsl9408b t42;
                            GM_ST(uint32_t, (uint8_t *)&t42 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t37);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42 + 16), t41);
                            gm_tftb1odc739t5 t43 = gm_f_push_hv483nm(t36, &t42);
                            gm_l9 = t43;
                            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srckg5f2c0p31ur, 238, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 233, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l4;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l4 = t48;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&gm_l2), 32);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_second_class_part_k6i54mk F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t6, GM_CONV(gm_csguankko4lmhd, t7));
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)15ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 201, 8);
    }
gm_lb1:;
    uint8_t t10 = gm_l2;
    if (t10) {
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_tkn42u7na3t81 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
        ((uint8_t *)&t12)[4] = 1;
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_tkn42u7na3t81 t15 = gm_f_inner_second_class_part_ufhro3f(t13, GM_CONV(gm_csguankko4lmhd, t14));
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4) {
    struct gm_fr_unsatisfied_jnmtj8v F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    uint32_t gm_l4; /* template */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l5;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = gm_l2;
        gm_b16_8 t6 = gm_f_constraint_name_ombut3o(t5);
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        uint32_t t9 = gm_l3;
        gm_b16_8 t10 = gm_f_param_name_rbnop5l(t8, t9);
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 1088));
        uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 1088) + 8));
        uint32_t t14 = gm_l4;
        uint64_t t15 = (uint64_t)t14;
        if ((uintptr_t)t15 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t15, t13, gm_srckg5f2c0p31ur, 63, 126);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t12) + (uintptr_t)t15 * 184));
        gm_t0uuhaljsf410 t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t6);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 32), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 48), t16);
        gm_f_format_980ugfo((void *)&gm_l5, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srckg5f2c0p31ur, 63, 12);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
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
extern __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_unscope_type_params_bofhhep(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_value_at_sthggua(void *gm_out, const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_at_sthggua F;
    gm_tkaapcii86udk gm_l0; /* p */
    uint64_t gm_l1; /* k */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* x */
    void * gm_l7; /* a */
    uint64_t gm_l8; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_t2lqu8jsj511d gm_l9;
    {
        {
            uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
            uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
            if (!t12) {
                goto gm_lb2;
            }
            uint64_t t13 = gm_l1;
            uint8_t t14 = (!(t13 == ((uint64_t)0ULL)));
            if (!t14) {
                goto gm_lb2;
            }
            memset(&(*(gm_tjoi8o798ne9k *)gm_out), 0, sizeof (*(gm_tjoi8o798ne9k *)gm_out));
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t15 = GM_LD(void *, ((uint8_t *)a0));
        memmove(((uint8_t *)&F.l2), ((uint8_t *)t15), 48);
    }
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t16;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb3;
            }
            {
                uint64_t t20 = gm_l4;
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t23 = gm_l4;
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
                gm_t2lqu8jsj511d t25;
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
                gm_l9 = t25;
                {
                    {
                        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        gm_l5 = t26;
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        gm_l6 = t27;
                        {
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)3ULL));
                                if (!t29) {
                                    goto gm_lb9;
                                }
                                void * t30 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                                gm_l7 = t30;
                                uint64_t t31 = gm_l5;
                                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                                if (o33) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 604, 17);
                                uint64_t t34 = r32;
                                uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
                                uint8_t t36 = ((t34 == t35));
                                if (t36) {
                                    uint64_t t37 = gm_l6;
                                    uint64_t t38 = gm_l1;
                                    uint64_t r39; bool o40 = __builtin_add_overflow(t37, t38, &r39);
                                    if (o40) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 604, 39);
                                    uint64_t t41 = r39;
                                    gm_l8 = t41;
                                } else {
                                    uint64_t t42 = gm_l6;
                                    gm_l8 = t42;
                                }
                                uint64_t t43 = gm_l8;
                                void * t44 = gm_l7;
                                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 8));
                                uint64_t t46 = (uint64_t)t45;
                                uint8_t t47 = (t43 >= t46);
                                if (t47) {
                                    memset(&(*(gm_tjoi8o798ne9k *)gm_out), 0, sizeof (*(gm_tjoi8o798ne9k *)gm_out));
                                    goto gm_fdone;
                                }
                                void * t48 = gm_l7;
                                void * t49 = GM_LD(void *, ((uint8_t *)t48));
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t48) + 8));
                                uint64_t t51 = gm_l8;
                                uint64_t t52 = (uint64_t)t51;
                                if ((uintptr_t)t52 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t52, t50, gm_srckg5f2c0p31ur, 606, 13);
                                memmove(((uint8_t *)&F.l2), (((uint8_t *)t49) + (uintptr_t)t52 * 48), 48);
                                goto gm_lb8;
                            }
gm_lb9:;
                            memset(&(*(gm_tjoi8o798ne9k *)gm_out), 0, sizeof (*(gm_tjoi8o798ne9k *)gm_out));
                            goto gm_fdone;
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 602, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t53 = gm_l4;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l4 = t56;
        }
    }
gm_lb3:;
    {
        gm_t6evimt84lndq t57 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 56, 0);
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out)), t57);
        ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out))[48] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjoi8o798ne9k gm_f_value_at_sthggua_c(gm_tkaapcii86udk a0, uint64_t a1) { gm_tjoi8o798ne9k gm_r; gm_f_value_at_sthggua(&gm_r, &a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_value_key_vki574i F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    unsigned __int128 gm_l5; /* bits */
    uint16_t gm_l6; /* b */
    uint32_t gm_l7; /* w */
    uint8_t gm_l9; /* b */
    uint32_t gm_l11; /* f */
    gm_to6glrduves6o gm_l12; /* sl */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    unsigned __int128 gm_l17; /* b */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 1570u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l21;
    uint8_t gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tjoi8o798ne9k gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tbccvikg6b96e gm_l26;
    gm_tftb1odc739t5 gm_l27;
    void * t30 = gm_l0;
    void * t31 = (void *)(((uint8_t *)t30) + 8);
    gm_l4 = t31;
    {
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
            if (!t33) {
                goto gm_lb2;
            }
            unsigned __int128 t34 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l5 = t34;
            void * t35 = gm_l4;
            gm_t88rcucg1b7uk t36 = gm_l2;
            uint8_t t37 = gm_f_is_integer_8d4f9ek(t35, GM_CONV(gm_csguankko4lmhd, t36));
            if (t37) {
                {
                    void * t38 = gm_l4;
                    gm_t88rcucg1b7uk t39 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l21, t38, GM_CONV(gm_csguankko4lmhd, t39));
                }
                {
                    {
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
                        if (!t41) {
                            goto gm_lb4;
                        }
                        uint16_t t42 = GM_LD(uint16_t, ((((uint8_t *)&gm_l21) + 8) + 2));
                        gm_l6 = t42;
                        uint16_t t43 = gm_l6;
                        uint32_t t44 = (uint32_t)t43;
                        gm_l7 = t44;
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        void * t45 = gm_l0;
                        void * t46 = GM_LD(void *, ((uint8_t *)t45));
                        void * t47 = (void *)(((uint8_t *)t46) + 88);
                        uint32_t t48 = gm_f_ptr_bits_glps5i7(t47);
                        gm_l7 = t48;
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 488, 22);
                }
gm_lb3:;
                {
                    unsigned __int128 t49 = gm_l5;
                    gm_f_from_u128_gl5fk0t((void *)&F.l8, (void *)NULL, cx, NULL, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        void * t50 = gm_l4;
                        gm_t88rcucg1b7uk t51 = gm_l2;
                        uint8_t t52 = gm_f_is_signed_integer_s66djm7(t50, GM_CONV(gm_csguankko4lmhd, t51));
                        if (!t52) {
                            goto gm_lb7;
                        }
                        uint32_t t53 = gm_l7;
                        uint8_t t54 = (t53 > ((uint32_t)0ULL));
                        if (!t54) {
                            goto gm_lb7;
                        }
                        unsigned __int128 t55 = gm_l5;
                        uint32_t t56 = gm_l7;
                        uint32_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint32_t)1ULL), &r57);
                        if (o58) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 493, 70);
                        uint32_t t59 = r57;
                        unsigned __int128 t60 = (unsigned __int128)t59;
                        unsigned __int128 n61 = (unsigned __int128)t60;
                        if (n61 >= 128) gm_trap_shift((uint64_t)n61, "u128", gm_srckg5f2c0p31ur, 493, 57);
                        unsigned __int128 t62 = (unsigned __int128)(t55 >> (int)n61);
                        unsigned __int128 t63 = (unsigned __int128)(t62 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                        uint8_t t64 = ((t63 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                        if (!t64) {
                            goto gm_lb7;
                        }
                        {
                            gm_tbci5ospk5nnn t65 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l8));
                            gm_tbci5ospk5nnn t66;
                            gm_f_one_08qg1op((void *)&t66, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t67 = gm_l7;
                            uint64_t t68 = (uint64_t)t67;
                            gm_tbci5ospk5nnn t69;
                            gm_f_shl_qqecl81((void *)&t69, (void *)NULL, cx, NULL, &t66, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_f_sub_pktm5cr((void *)&F.l8, (void *)NULL, cx, NULL, &t65, &t69);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
                {
                    void * t70 = gm_l0;
                    gm_t88rcucg1b7uk t71 = gm_l2;
                    gm_t428ue2u3hf5g t72;
                    gm_f_int_key_dmcraup((void *)&t72, (void *)NULL, cx, NULL, t70, t71, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t72);
                    ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                    goto gm_fdone;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)2ULL));
            if (!t74) {
                goto gm_lb8;
            }
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l9 = t75;
            {
                gm_trqfvmjtt4sij t76;
                gm_f_list_uvfu1k1((void *)&t76, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l10 = t76;
            }
            void * t77 = (void *)((uint8_t *)&F.l10);
            uint8_t t78 = gm_l9;
            if (t78) {
                gm_l22 = ((uint8_t)1ULL);
            } else {
                gm_l22 = ((uint8_t)0ULL);
            }
            uint8_t t79 = gm_l22;
            gm_tftb1odc739t5 t80 = gm_f_push_t83rg2m(t77, t79);
            gm_l23 = t80;
            uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srckg5f2c0p31ur, 498, 13);
            {
                gm_t88rcucg1b7uk t82 = gm_l2;
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                gm_tcmkompbl09il t85;
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85 + 0), t82);
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 8), t84);
                gm_t428ue2u3hf5g t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t86 + 8), t85);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t86);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb8:;
        {
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)11ULL));
            if (!t88) {
                goto gm_lb9;
            }
            uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l11 = t89;
            {
                uint32_t t90 = gm_l11;
                void * t91 = gm_l0;
                void * t92 = GM_LD(void *, (((uint8_t *)t91) + 344));
                uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 344) + 8));
                uint32_t t94 = gm_l11;
                uint64_t t95 = (uint64_t)t94;
                if ((uintptr_t)t95 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t95, t93, gm_srckg5f2c0p31ur, 501, 33);
                gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)t92) + (uintptr_t)t95 * 136));
                gm_tiilmfd16v3fc t97;
                GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t97 + 0), t90);
                GM_ST(gm_b16_8, ((uint8_t *)&t97 + 8), t96);
                gm_t428ue2u3hf5g t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t98 + 8), t97);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t98);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb9:;
        {
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)6ULL));
            if (!t100) {
                goto gm_lb10;
            }
            {
                gm_t428ue2u3hf5g t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t101);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb10:;
        {
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)5ULL));
            if (!t103) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&gm_l12), (((uint8_t *)&F.l1) + 16), 32);
            gm_t88rcucg1b7uk t104 = gm_l2;
            void * t105 = gm_l4;
            gm_t88rcucg1b7uk t106 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t107 = (gm_eq9m6mvtmgde(((uint8_t *)&t104), ((uint8_t *)&t106)));
            if (t107) {
                {
                    gm_trqfvmjtt4sij t108;
                    gm_f_list_uvfu1k1((void *)&t108, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l13 = t108;
                }
                gm_l15 = ((uint64_t)0ULL);
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 24));
                gm_l14 = t109;
                {
                    for (;;) {
                        uint64_t t110 = gm_l15;
                        uint64_t t111 = gm_l14;
                        uint8_t t112 = (t110 < t111);
                        if (!t112) {
                            goto gm_lb12;
                        }
                        {
                            {
                                {
                                    uint64_t t113 = gm_l15;
                                    gm_l16 = t113;
                                    {
                                        {
                                            {
                                                uint64_t t114 = gm_l16;
                                                gm_tkaapcii86udk t115 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l12));
                                                gm_f_value_at_sthggua((void *)&gm_l24, &t115, t114);
                                            }
                                            uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l24))[48] != 0);
                                            if (!t116) {
                                                goto gm_lb18;
                                            }
                                            uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)0ULL));
                                            if (!t118) {
                                                goto gm_lb18;
                                            }
                                            unsigned __int128 t119 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l24) + 16));
                                            gm_l17 = t119;
                                            void * t120 = (void *)((uint8_t *)&F.l13);
                                            unsigned __int128 t121 = gm_l17;
                                            uint8_t t122 = (uint8_t)t121;
                                            gm_tftb1odc739t5 t123 = gm_f_push_t83rg2m(t120, t122);
                                            gm_l25 = t123;
                                            uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                            if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srckg5f2c0p31ur, 510, 17);
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        void * t125 = gm_l0;
                                        gm_tl874t2atnlh3 t126 = gm_l3;
                                        gm_b16_8 t127;
                                        GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sicfped8s368m));
                                        GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(57));
                                        gm_b0_1 t128 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t125, t126, t127);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
                                        goto gm_fdone;
                                    }
gm_lb17:;
                                    goto gm_lb15;
                                }
gm_lb16:;
                                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 505, 13);
                            }
gm_lb15:;
                        }
gm_lb14:;
                        uint64_t t129 = gm_l15;
                        uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                        uint64_t t132 = r130;
                        gm_l15 = t132;
                    }
                }
gm_lb12:;
                {
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                    gm_t428ue2u3hf5g t135;
                    GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t135 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t135 + 8), t134);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t135);
                    ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                    goto gm_fdone;
                }
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            {
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)1ULL));
                    if (!t137) {
                        goto gm_lb21;
                    }
                    goto gm_lb20;
                }
gm_lb21:;
                {
                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)3ULL));
                    if (!t139) {
                        goto gm_lb22;
                    }
                    goto gm_lb20;
                }
gm_lb22:;
                goto gm_lb19;
            }
gm_lb20:;
            {
                gm_trqfvmjtt4sij t140;
                gm_f_list_uvfu1k1((void *)&t140, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l18 = t140;
            }
            gm_l20 = ((uint64_t)0ULL);
            {
                void * t141 = gm_l0;
                gm_t88rcucg1b7uk t142 = gm_l2;
                gm_f_layout_a5ovnrm((void *)&gm_l26, (void *)NULL, cx, NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
            uint64_t t144 = (uint64_t)t143;
            gm_l19 = t144;
            {
                for (;;) {
                    uint64_t t145 = gm_l20;
                    uint64_t t146 = gm_l19;
                    uint8_t t147 = (t145 < t146);
                    if (!t147) {
                        goto gm_lb23;
                    }
                    {
                        {
                            {
                                void * t148 = (void *)((uint8_t *)&F.l18);
                                gm_tftb1odc739t5 t149 = gm_f_push_t83rg2m(t148, ((uint8_t)0ULL));
                                gm_l27 = t149;
                                uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srckg5f2c0p31ur, 517, 58);
                                goto gm_lb26;
                            }
gm_lb27:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 517, 13);
                        }
gm_lb26:;
                    }
gm_lb25:;
                    uint64_t t151 = gm_l20;
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    uint64_t t154 = r152;
                    gm_l20 = t154;
                }
            }
gm_lb23:;
            {
                void * t155 = gm_l0;
                gm_t88rcucg1b7uk t156 = gm_l2;
                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                uint8_t t158 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t155, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t156, t157);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t158) {
                    gm_t88rcucg1b7uk t159 = gm_l2;
                    gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                    gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                    gm_tcmkompbl09il t162;
                    GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t162 + 0), t159);
                    GM_ST(gm_b16_8, ((uint8_t *)&t162 + 8), t161);
                    gm_t428ue2u3hf5g t163;
                    GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t163 + 8), t162);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out)), t163);
                    ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                    goto gm_fdone;
                }
            }
            goto gm_lb1;
        }
gm_lb19:;
        {
            goto gm_lb1;
        }
gm_lb28:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 486, 5);
    }
gm_lb1:;
    void * t164 = gm_l0;
    gm_tl874t2atnlh3 t165 = gm_l3;
    gm_b16_8 t166;
    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sd2s2op6i577j));
    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(35));
    gm_b0_1 t167 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t164, t165, t166);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    memset(&(*(gm_trgmlgt2gmmjs *)gm_out), 0, sizeof (*(gm_trgmlgt2gmmjs *)gm_out));
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

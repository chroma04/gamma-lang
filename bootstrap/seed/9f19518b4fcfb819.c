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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; } f1; } gm_tu0a56t4bfvqh;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_tb59ptmmofnb5;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_tn68el8d14vku v1; } f1; } gm_tsncjtib3j1l1;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_tu0a56t4bfvqh) == 16 && _Alignof(gm_tu0a56t4bfvqh) == 8 && offsetof(gm_tu0a56t4bfvqh, f0) == 0 && offsetof(gm_tu0a56t4bfvqh, f1) == 8, "layout of `Base`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_tsncjtib3j1l1 f0; uint8_t f1; uint8_t pe[7]; } gm_t1335qftqvd8f;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tf1bjg81u0glv f0; uint8_t f1; uint8_t pe[7]; } gm_t8sds1gdern44;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb59ptmmofnb5) == 24 && _Alignof(gm_tb59ptmmofnb5) == 8 && offsetof(gm_tb59ptmmofnb5, f0) == 0 && offsetof(gm_tb59ptmmofnb5, f1) == 16 && offsetof(gm_tb59ptmmofnb5, f2) == 20, "layout of `LocalFact`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint8_t pe[4]; } gm_tfqiukge1b9q0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_tt7dbdq4n1egc f0; uint8_t f1; uint8_t pe[3]; } gm_tkol5reu9t1u2;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_tkn42u7na3t81 f0; uint8_t f1; uint8_t pe[3]; } gm_tpk1phrp285m3;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsncjtib3j1l1) == 24 && _Alignof(gm_tsncjtib3j1l1) == 8 && offsetof(gm_tsncjtib3j1l1, f0) == 0 && offsetof(gm_tsncjtib3j1l1, f1) == 8, "layout of `DeclRef`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_tb59ptmmofnb5 f0; uint8_t f1; uint8_t pe[7]; } gm_tvsjp38cd9njc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1335qftqvd8f) == 32 && _Alignof(gm_t1335qftqvd8f) == 8 && offsetof(gm_t1335qftqvd8f, f0) == 0 && offsetof(gm_t1335qftqvd8f, f1) == 24, "layout of `?DeclRef`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_tfqiukge1b9q0 f0; uint8_t f1; uint8_t pe[7]; } gm_t3jh0etmvddmg;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t6ok5tvcjc200 f1; } gm_t70bah4iduq3d;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8sds1gdern44) == 32 && _Alignof(gm_t8sds1gdern44) == 8 && offsetof(gm_t8sds1gdern44, f0) == 0 && offsetof(gm_t8sds1gdern44, f1) == 24, "layout of `?(usize, string)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tfqiukge1b9q0) == 48 && _Alignof(gm_tfqiukge1b9q0) == 8 && offsetof(gm_tfqiukge1b9q0, f0) == 0 && offsetof(gm_tfqiukge1b9q0, f1) == 40, "layout of `(q: List(u32), r: u32)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkol5reu9t1u2) == 32 && _Alignof(gm_tkol5reu9t1u2) == 4 && offsetof(gm_tkol5reu9t1u2, f0) == 0 && offsetof(gm_tkol5reu9t1u2, f1) == 28, "layout of `?Found`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpk1phrp285m3) == 12 && _Alignof(gm_tpk1phrp285m3) == 4 && offsetof(gm_tpk1phrp285m3, f0) == 0 && offsetof(gm_tpk1phrp285m3, f1) == 8, "layout of `?TypeFact`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { void * f0; void * f1; void * f2; uint64_t f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_t4c7ipjj9s3fp f6; gm_t4c7ipjj9s3fp f7; gm_t4c7ipjj9s3fp f8; gm_t1335qftqvd8f f9; } gm_tt5bocau6bbvj;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tvsjp38cd9njc) == 32 && _Alignof(gm_tvsjp38cd9njc) == 8 && offsetof(gm_tvsjp38cd9njc, f0) == 0 && offsetof(gm_tvsjp38cd9njc, f1) == 24, "layout of `?LocalFact`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t3jh0etmvddmg) == 56 && _Alignof(gm_t3jh0etmvddmg) == 8 && offsetof(gm_t3jh0etmvddmg, f0) == 0 && offsetof(gm_t3jh0etmvddmg, f1) == 48, "layout of `?Scope`");
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70bah4iduq3d) == 48 && _Alignof(gm_t70bah4iduq3d) == 8 && offsetof(gm_t70bah4iduq3d, f0) == 0 && offsetof(gm_t70bah4iduq3d, f1) == 8, "layout of `(kind: Kind, hover: Hover)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_tt5bocau6bbvj) == 312 && _Alignof(gm_tt5bocau6bbvj) == 8 && offsetof(gm_tt5bocau6bbvj, f0) == 0 && offsetof(gm_tt5bocau6bbvj, f1) == 8 && offsetof(gm_tt5bocau6bbvj, f2) == 16 && offsetof(gm_tt5bocau6bbvj, f3) == 24 && offsetof(gm_tt5bocau6bbvj, f4) == 32 && offsetof(gm_tt5bocau6bbvj, f5) == 72 && offsetof(gm_tt5bocau6bbvj, f6) == 112 && offsetof(gm_tt5bocau6bbvj, f7) == 168 && offsetof(gm_tt5bocau6bbvj, f8) == 224 && offsetof(gm_tt5bocau6bbvj, f9) == 280, "layout of `Walker`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t6qooom8qa2do f0; uint8_t f1; uint8_t pe[7]; } gm_tlabdfckddbfd;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tlabdfckddbfd) == 216 && _Alignof(gm_tlabdfckddbfd) == 8 && offsetof(gm_tlabdfckddbfd, f0) == 0 && offsetof(gm_tlabdfckddbfd, f1) == 208, "layout of `?DeclValue`");
struct gm_fr_add_ref_tgbe5vc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_arg_amoajjv {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_tc3oht4ksth9d l3; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_b0_1 ret;
};
struct gm_fr_base_of_9tbrpvr {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tu0a56t4bfvqh ret;
};
struct gm_fr_block_jfodv9u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_call_8vfr61j {
    gm_frame h;
    gm_t5v0c8ia6a5mo l26; /* p */
    gm_b0_1 ret;
};
struct gm_fr_classify_pdaj2gb {
    gm_frame h;
    gm_tu0a56t4bfvqh ret;
};
struct gm_fr_cond_d94lif5 {
    gm_frame h;
    gm_t0d02142k4tu8 l5; /* pat */
    gm_b0_1 ret;
};
struct gm_fr_contains_2dnkft5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_475f687 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_cra391b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_name_span_ht2nhb5 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_declare_feg3ue0 {
    gm_frame h;
    gm_tl5cuhjfnc4cr l10; /* item */
    gm_b0_1 ret;
};
struct gm_fr_def_once_pqgvduh {
    gm_frame h;
    gm_t6ok5tvcjc200 l2; /* h */
    gm_b0_1 ret;
};
struct gm_fr_effect_lit_26tkk47 {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    void * ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_537clvp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6ld1tun {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8td41o0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cln8ptp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jdnmbru {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5p0uct {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ensure_hover_3epdjmk {
    gm_frame h;
    gm_t70bah4iduq3d l5; /* info */
    gm_t70bah4iduq3d l8; /* info */
    gm_b0_1 ret;
};
struct gm_fr_expr_kih3eo8 {
    gm_frame h;
    void * l0; /* w */
    gm_t1s75i08qtptd l2; /* label */
    gm_tioi3jh9v22g5 l5; /* x */
    gm_tlivgedjs5ps3 l18; /* sg */
    gm_t79rbdv4rafe5 l28; /* b */
    gm_tchr52g0ema53 l29; /* x */
    gm_b0_1 ret;
};
struct gm_fr_field_7p9pmjo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_field_use_838f8e8 {
    gm_frame h;
    gm_tpautr4br7h3l l9; /* def */
    uint8_t ret;
};
struct gm_fr_find_field_l0p9325 {
    gm_frame h;
    gm_tfc3oljlofib6 l5; /* m */
    gm_t24301fas1b2a l6; /* f */
    gm_t7bjbmukv26e7 l10; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_find_payload_field_i16vrf1 {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_tv1o1j44ii4db l12; /* el */
    gm_t7bjbmukv26e7 l14; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_find_variant_kifk1mm {
    gm_frame h;
    gm_t03a3imodqi32 l5; /* m */
    gm_tnljbma3s3h16 l6; /* v */
    gm_t7bjbmukv26e7 l7; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_fn_rqpk5og {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_use_l994th9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_expr_f16jm2b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_ahgk48j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ekcps7q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6rro2t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_1ji4too {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_o4mipv6 {
    gm_frame h;
    gm_tpk1phrp285m3 ret;
};
struct gm_fr_get_rlnefnq {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_hash_map_7pa51u7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_oh3lgki {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s7cd6p5 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_if_expr_6oqub48 {
    gm_frame h;
    gm_ttja7ii61nila l6; /* el */
    gm_t79rbdv4rafe5 l7; /* b */
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_use_9lcr9t0 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_list_nrf6taq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uc1atd0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_binding_25t1g13 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lookup_decl_10pnl6c {
    gm_frame h;
    gm_t1335qftqvd8f ret;
};
struct gm_fr_lookup_local_tdg7skl {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_match_expr_j2bvhsh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_member_def_9f4kiah {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_toceffb697p0u ret;
};
struct gm_fr_name_use_73pph7t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_receiver_6mb4kc5 {
    gm_frame h;
    gm_t6qooom8qa2do l6; /* v */
    gm_trqfvmjtt4sij l9; /* items */
    gm_b0_1 ret;
};
struct gm_fr_operation_use_ue1or58 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_path_type_flfaob1 {
    gm_frame h;
    gm_t6qooom8qa2do l5; /* v */
    gm_t6qooom8qa2do l9; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pattern_jo09k6f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_payload_field_3o532rn {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_b0_1 ret;
};
struct gm_fr_pop_amp5ob6 {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_pop_g2usrvd_2 {
    gm_frame h;
    gm_tfqiukge1b9q0 l2; /* x */
    gm_t3jh0etmvddmg ret;
};
struct gm_fr_pop_label_e8oj3h3 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_pop_scope_vgka5ae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_bb3f68j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_label_gg2j5c3 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_push_pcqb56b_2 {
    gm_frame h;
    gm_tfqiukge1b9q0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rb04a8h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_scope_u4jfguj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_sige3eu {
    gm_frame h;
    gm_tfgiovingnnea l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_ref_key_vam5ppg {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_8skjmj4_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_b5mmd35 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cvgrcrj_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q6son6o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rgq4vgc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_sig_89f514c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stmt_ald930b {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_t43d6nbdfap6p l12; /* x */
    gm_t0d02142k4tu8 l13; /* pat */
    gm_t79rbdv4rafe5 l17; /* b */
    gm_b0_1 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_type_literal_nnom1cv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b0_1 ret;
};
struct gm_fr_type_of_tiqb21n {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_use_decl_sfao4j5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variant_at_9i78oa0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* tys */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_variant_payload_3id0koh {
    gm_frame h;
    gm_tckdobunaulee l10; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_variant_use_29ooua3 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_walker_8qtbjaj {
    gm_frame h;
    gm_tt5bocau6bbvj l4; /* w */
    gm_t6qooom8qa2do l8; /* v */
    gm_tt5bocau6bbvj ret;
};
struct gm_fr_when_stmt_r5g5fsm {
    gm_frame h;
    gm_tapdr2ag17fh7 l2; /* el */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_ref_tgbe5vc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_arg_amoajjv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_base_of_9tbrpvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_jfodv9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_8vfr61j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_classify_pdaj2gb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_cond_d94lif5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_2dnkft5(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_475f687(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_cra391b(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5_c(void * a0, gm_tsncjtib3j1l1 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_feg3ue0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void * gm_f_effect_lit_26tkk47(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_537clvp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8td41o0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cln8ptp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jdnmbru(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5p0uct(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_ensure_hover_3epdjmk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_kih3eo8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_field_7p9pmjo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_field_use_838f8e8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_1ff6h79(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_bhdmq1t(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_dlnutct(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_field_l0p9325(gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hit3u63(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_krs565o(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_payload_field_i16vrf1(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_find_variant_kifk1mm(gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_literal_sodo3l0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_use_l994th9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_expr_f16jm2b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ekcps7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_b2m4si4(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tpk1phrp285m3 gm_f_get_o4mipv6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_o6c8ntf(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_shqk64n(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_literal_bbdui98(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_7pa51u7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_oh3lgki(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_6oqub48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_label_use_9lcr9t0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_nrf6taq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rk274m7_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_local_binding_25t1g13(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_decl_10pnl6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_lookup_local_tdg7skl(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_expr_j2bvhsh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_member_def_9f4kiah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_name_use_73pph7t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_receiver_6mb4kc5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_operation_use_ue1or58(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_params_ufsghpd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_path_type_flfaob1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_jo09k6f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_payload_field_3o532rn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_amp5ob6(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd_2(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3_c(void * a0, gm_t1s75i08qtptd a1);
inline __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_label_gg2j5c3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sige3eu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_1rbrft2(void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_hku1qp6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_rt5d34n(void * a0, gm_cseahvg8v7p0vs a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_sokl06f(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ss3ma85(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg_c(gm_tsncjtib3j1l1 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q6son6o(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rgq4vgc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_rest_gkcl8d2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_ald930b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_lit_lm0h7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_tiqb21n(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_use_decl_sfao4j5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_at_9i78oa0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_variant_payload_3id0koh(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_use_29ooua3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) void gm_f_walker_8qtbjaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_r5g5fsm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s3m5ql5voef28[6];
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_siuacbf0blrje[9];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb2hdor4f6g0q[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const gm_tmpl gm_tplalgvrfo85soq;
extern const gm_tmpl gm_tple75bobdc6q0g;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_2dnkft5(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_2dnkft5 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_bhdmq1t(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_475f687(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_475f687 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_krs565o(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_cra391b(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_cra391b F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_1ff6h79(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_def_once_pqgvduh F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 16));
    void * t4 = (void *)(((uint8_t *)t3) + 40);
    gm_tl874t2atnlh3 t5 = gm_l1;
    uint8_t t6 = gm_f_contains_cra391b(t4, GM_CONV(gm_cseahvg8v7p0vs, t5));
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
        void * t10 = (void *)(((uint8_t *)t9) + 40);
        gm_tl874t2atnlh3 t11 = gm_l1;
        gm_tftb1odc739t5 t12 = gm_f_put_rt5d34n(t10, GM_CONV(gm_cseahvg8v7p0vs, t11), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
        gm_l3 = t12;
        uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcb2hdor4f6g0q, 149, 40);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1ivdmti F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_537clvp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_537clvp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6ld1tun F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8td41o0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8td41o0 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cln8ptp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cln8ptp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jdnmbru(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jdnmbru F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5p0uct(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5p0uct F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_rqpk5og F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_kih3eo8 *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ekcps7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ekcps7q F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siuacbf0blrje));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplalgvrfo85soq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f6rro2t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8gf7bnv27fi9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4476c5bksft1), t2);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_1ji4too F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_bhdmq1t(t2, t3);
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
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 16) + 8));
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
inline __attribute__((aligned(4))) gm_tpk1phrp285m3 gm_f_get_o4mipv6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_o4mipv6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_hit3u63(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tpk1phrp285m3 t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tkn42u7na3t81 t12 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tpk1phrp285m3 t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_rlnefnq F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_dlnutct(t2, t3);
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
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 20) + 16));
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
inline __attribute__((aligned(4))) void gm_f_hash_map_7pa51u7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_7pa51u7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_cln8ptp();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_oh3lgki(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_oh3lgki F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n5p0uct();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_s7cd6p5 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1ivdmti();
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
inline __attribute__((aligned(4))) void gm_f_list_nrf6taq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nrf6taq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jdnmbru();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jbu9fo3_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lv6i4jh_2();
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
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uc1atd0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6ld1tun();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_amp5ob6(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_amp5ob6 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t08olq97rscn3 gm_l2; /* x */
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
        memset(&(*(gm_t1s75i08qtptd *)gm_out), 0, sizeof (*(gm_t1s75i08qtptd *)gm_out));
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
        memmove(((uint8_t *)&gm_l2), (((uint8_t *)t7) + (uintptr_t)t12 * 32), 32);
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
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out)), t25);
        ((uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out))[32] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd_2(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_g2usrvd_2 F;
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
        memset(&(*(gm_t3jh0etmvddmg *)gm_out), 0, sizeof (*(gm_t3jh0etmvddmg *)gm_out));
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
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 48), 48);
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
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_tfqiukge1b9q0 t25 = GM_LD(gm_tfqiukge1b9q0, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 52, 0);
        GM_ST(gm_tfqiukge1b9q0, ((uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out)), t25);
        ((uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out))[48] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_label_e8oj3h3 F;
    void * gm_l0; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l2;
    {
        {
            uint8_t t5 = (uint8_t)((((uint8_t *)&F.l1))[32] != 0);
            if (t5) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 732, 8);
    }
gm_lb1:;
    uint8_t t6 = gm_l2;
    if (t6) {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 72);
        gm_t1s75i08qtptd t9;
        gm_f_pop_amp5ob6((void *)&t9, t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_vgka5ae F;
    void * gm_l0; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 32);
    gm_t3jh0etmvddmg t4;
    gm_f_pop_g2usrvd_2((void *)&t4, t3);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bb3f68j F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jdnmbru();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_rgq4vgc(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn_2 F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pcqb56b_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lv6i4jh_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_cvgrcrj_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rb04a8h F;
    void * gm_l0; /* l */
    gm_t2p74ki8sl4an gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_8td41o0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q6son6o(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_push_scope_u4jfguj F;
    void * gm_l0; /* w */
    uint32_t gm_l1; /* end */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 32);
        gm_trqfvmjtt4sij t4;
        gm_f_list_nrf6taq((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t5 = gm_l1;
        gm_tfqiukge1b9q0 t6;
        GM_ST(uint32_t, (uint8_t *)&t6 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 0), t4);
        GM_ST(uint32_t, ((uint8_t *)&t6 + 40), t5);
        gm_tftb1odc739t5 t7 = gm_f_push_pcqb56b_2(t3, &t6);
        gm_l2 = t7;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcb2hdor4f6g0q, 126, 5);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sige3eu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sige3eu F;
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
        gm_b16_8 t9 = gm_f_empty_slice_537clvp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_b5mmd35(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)a1), 88);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_b5mmd35 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cvgrcrj_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q6son6o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q6son6o F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rgq4vgc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_rgq4vgc F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_sig_89f514c F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 56) + 8));
    gm_b0_1 t5 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = gm_l0;
    void * t7 = gm_l1;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b0_1 t10 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t6, t9, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = gm_l0;
    void * t12 = gm_l1;
    gm_b0_1 t13 = gm_f_sig_rest_gkcl8d2((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t14 = gm_l0;
    gm_f_pop_scope_vgka5ae(t14);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_type_literal_nnom1cv F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uc1atd0((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = gm_l0;
    void * t4 = gm_l1;
    void * t5 = (void *)((uint8_t *)&F.l2);
    gm_b0_1 t6 = gm_f_type_lit_lm0h7k6((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_ref_tgbe5vc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_add_ref_tgbe5vc F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    gm_tl874t2atnlh3 gm_l2; /* to */
    GM_STACK_CHECK(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            gm_tl874t2atnlh3 t4 = gm_l1;
            gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            uint8_t t6 = (!gm_eqff85ac10o3(((uint8_t *)&t4), ((uint8_t *)&t5)));
            if (!t6) {
                goto gm_lb2;
            }
            gm_tl874t2atnlh3 t7 = gm_l2;
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            uint8_t t9 = (!gm_eqff85ac10o3(((uint8_t *)&t7), ((uint8_t *)&t8)));
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 16));
            void * t12 = (void *)((uint8_t *)t11);
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_tl874t2atnlh3 t14 = gm_l2;
            gm_t2p74ki8sl4an t15;
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t15 + 0), t13);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t15 + 12), t14);
            gm_tftb1odc739t5 t16 = gm_f_push_rb04a8h(t12, &t15);
            gm_l3 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcb2hdor4f6g0q, 173, 62);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_arg_amoajjv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_arg_amoajjv F;
    void * gm_l0; /* w */
    void * gm_l1; /* a */
    GM_STACK_CHECK(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t6 = gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t6) + 8), 88);
            void * t9 = gm_l0;
            void * t10 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t11 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t6) + 8) + 32), 88);
            void * t14 = gm_l0;
            void * t15 = (void *)((uint8_t *)&F.l3);
            gm_b0_1 t16 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t6) + 8), 88);
            void * t19 = gm_l0;
            void * t20 = (void *)((uint8_t *)&F.l4);
            gm_b0_1 t21 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 489, 5);
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
inline __attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_base_of_9tbrpvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_base_of_9tbrpvr F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* x */
    uint64_t gm_l4; /* m */
    GM_STACK_CHECK(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tlabdfckddbfd gm_l5;
    gm_tu0a56t4bfvqh gm_l6;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            gm_l2 = t14;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, ((uint8_t *)t15));
            uint64_t t17 = gm_l2;
            gm_f_decl_done_mk70ol9((void *)&gm_l5, (void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l5))[208] != 0);
            if (t18) {
                memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l5), 208);
            } else {
                gm_tu0a56t4bfvqh t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)3ULL));
                F.ret = t19;
                goto gm_fret;
            }
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)9ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                    gm_l4 = t22;
                    uint64_t t23 = gm_l4;
                    gm_tu0a56t4bfvqh t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t24 + 8), t23);
                    gm_l6 = t24;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        {
                            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
                            if (!t26) {
                                goto gm_lb7;
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                            if (!t28) {
                                goto gm_lb8;
                            }
                            goto gm_lb6;
                        }
gm_lb8:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    uint64_t t29 = gm_l2;
                    gm_tu0a56t4bfvqh t30;
                    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t30 + 8), t29);
                    gm_l6 = t30;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
                    if (!t32) {
                        goto gm_lb9;
                    }
                    uint64_t t33 = gm_l2;
                    gm_tu0a56t4bfvqh t34;
                    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t34 + 8), t33);
                    gm_l6 = t34;
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    gm_tu0a56t4bfvqh t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
                    gm_l6 = t35;
                    goto gm_lb3;
                }
gm_lb10:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 272, 12);
            }
gm_lb3:;
            gm_tu0a56t4bfvqh t36 = gm_l6;
            F.ret = t36;
            goto gm_fret;
        }
gm_lb2:;
        gm_tu0a56t4bfvqh t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)3ULL));
        F.ret = t37;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_block_jfodv9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_jfodv9u F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    void * t8 = gm_l1;
    uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t8) + 24) + 8));
    gm_b0_1 t10 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t7, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t11 = gm_l1;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 8) + 8));
    gm_l2 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = gm_l2;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l3;
                        gm_l4 = t16;
                        void * t17 = gm_l0;
                        void * t18 = gm_l1;
                        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
                        uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 8) + 8));
                        uint64_t t21 = gm_l4;
                        if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srcb2hdor4f6g0q, 604, 39);
                        void * t22 = (void *)(((uint8_t *)t19) + (uintptr_t)t21 * 144);
                        gm_b0_1 t23 = gm_f_stmt_ald930b((void *)NULL, cx, NULL, t17, t22);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 604, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l3;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l3 = t27;
        }
    }
gm_lb1:;
    void * t28 = gm_l0;
    gm_f_pop_scope_vgka5ae(t28);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_call_8vfr61j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_call_8vfr61j F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_t9805qtonbsag gm_l4; /* target */
    gm_t8sds1gdern44 gm_l5; /* variant */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    gm_tu0a56t4bfvqh gm_l8; /* cls */
    uint8_t gm_l9; /* is_field */
    gm_t88rcucg1b7uk gm_l10; /* t */
    uint32_t gm_l11; /* f */
    uint64_t gm_l12; /* d */
    gm_t08olq97rscn3 gm_l13; /* id */
    uint64_t gm_l14; /* d */
    gm_tf5srh4mp4b3i gm_l15; /* struct_decl */
    gm_t88rcucg1b7uk gm_l16; /* t */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    void * gm_l20; /* a */
    gm_t08olq97rscn3 gm_l21; /* n */
    gm_tf1bjg81u0glv gm_l22; /* v */
    uint32_t gm_l23; /* f */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l27; /* d */
    gm_tl874t2atnlh3 gm_l28; /* to */
    GM_STACK_CHECK(&F, 1580u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tkn42u7na3t81 gm_l29;
    gm_tf5srh4mp4b3i gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    gm_tkn42u7na3t81 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_toceffb697p0u gm_l35;
    {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 8));
        void * t46 = (void *)(((uint8_t *)t45) + 112);
        void * t47 = gm_l1;
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
        gm_t9805qtonbsag t49 = gm_f_get_rlnefnq(t46, GM_CONV(gm_cseahvg8v7p0vs, t48));
        gm_l4 = t49;
        memset(&gm_l5, 0, sizeof gm_l5);
        void * t50 = gm_l2;
        {
            {
                uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
                uint8_t t52 = (uint8_t)(t51 == ((uint8_t)22ULL));
                if (!t52) {
                    goto gm_lb2;
                }
                void * t53 = GM_LD(void *, ((((uint8_t *)t50) + 8) + 8));
                gm_l6 = t53;
                memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t50) + 8) + 8) + 8), 32);
                void * t54 = gm_l0;
                void * t55 = gm_l6;
                gm_tu0a56t4bfvqh t56 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t56;
                gm_tu0a56t4bfvqh t57 = gm_l8;
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                uint8_t t59 = (uint8_t)((t58 == ((uint8_t)3ULL)));
                gm_tu0a56t4bfvqh t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)3ULL));
                if (t59) {
                    void * t61 = gm_l0;
                    void * t62 = gm_l6;
                    gm_b0_1 t63 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t61, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = ((uint8_t)0);
                    {
                        {
                            void * t64 = gm_l0;
                            void * t65 = gm_l6;
                            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                            gm_tkn42u7na3t81 t67 = gm_f_type_of_tiqb21n(t64, GM_CONV(gm_cseahvg8v7p0vs, t66));
                            gm_l29 = t67;
                            uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l29))[4] != 0);
                            if (!t68) {
                                goto gm_lb4;
                            }
                            gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l29));
                            gm_l10 = t69;
                            void * t70 = gm_l0;
                            gm_t88rcucg1b7uk t71 = gm_l10;
                            uint8_t t72 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t70, t71, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l9 = t72;
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    uint8_t t73 = gm_l9;
                    uint8_t t74 = (uint8_t)!t73;
                    if (t74) {
                        {
                            {
                                uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                                if (!t75) {
                                    goto gm_lb6;
                                }
                                uint32_t t76 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                                gm_l11 = t76;
                                void * t77 = gm_l0;
                                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                                uint32_t t79 = gm_l11;
                                gm_b0_1 t80 = gm_f_fn_use_l994th9((void *)NULL, cx, NULL, t77, t78, t79);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb5;
                            }
gm_lb6:;
                        }
gm_lb5:;
                    }
                } else {
                    {
                        {
                            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)1ULL));
                            if (!t82) {
                                goto gm_lb8;
                            }
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            gm_l12 = t83;
                            {
                                uint64_t t84 = gm_l12;
                                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                gm_tf1bjg81u0glv t86;
                                GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t84);
                                GM_ST(gm_b16_8, ((uint8_t *)&t86 + 8), t85);
                                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&gm_l5), t86);
                                ((uint8_t *)&gm_l5)[24] = 1;
                            }
                            goto gm_lb7;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    void * t87 = gm_l0;
                    void * t88 = gm_l2;
                    gm_b0_1 t89 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
                uint8_t t91 = (uint8_t)(t90 == ((uint8_t)8ULL));
                if (!t91) {
                    goto gm_lb9;
                }
                memmove(((uint8_t *)&gm_l13), ((((uint8_t *)t50) + 8) + 8), 32);
                {
                    {
                        {
                            void * t92 = gm_l0;
                            void * t93 = gm_l1;
                            gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
                            gm_tf5srh4mp4b3i t95 = gm_f_variant_at_9i78oa0((void *)NULL, cx, NULL, t92, t94, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l13)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l30 = t95;
                        }
                        uint8_t t96 = (uint8_t)((((uint8_t *)&gm_l30))[8] != 0);
                        if (!t96) {
                            goto gm_lb11;
                        }
                        uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l30));
                        gm_l14 = t97;
                        {
                            uint64_t t98 = gm_l14;
                            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                            gm_tf1bjg81u0glv t100;
                            GM_ST(uint64_t, ((uint8_t *)&t100 + 0), t98);
                            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 8), t99);
                            GM_ST(uint8_t, (uint8_t *)&gm_l5 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l5 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l5 + 28, 0);
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&gm_l5), t100);
                            ((uint8_t *)&gm_l5)[24] = 1;
                        }
                        goto gm_lb10;
                    }
gm_lb11:;
                }
gm_lb10:;
                goto gm_lb1;
            }
gm_lb9:;
            {
                void * t101 = gm_l0;
                void * t102 = gm_l2;
                gm_b0_1 t103 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t101, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb12:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 543, 5);
        }
gm_lb1:;
    }
    {
        gm_tf5srh4mp4b3i t104;
        memset(&t104, 0, sizeof t104);
        gm_l15 = t104;
        {
            {
                {
                    {
                        uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                        if (t105) {
                            goto gm_lb16;
                        }
                        gm_l31 = ((uint8_t)1);
                        goto gm_lb15;
                    }
gm_lb16:;
                    {
                        gm_l31 = ((uint8_t)0);
                        goto gm_lb15;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 565, 8);
                }
gm_lb15:;
                uint8_t t106 = gm_l31;
                if (!t106) {
                    goto gm_lb14;
                }
                {
                    {
                        uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
                        if (t107) {
                            goto gm_lb19;
                        }
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb18;
                    }
gm_lb19:;
                    {
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb18;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 565, 27);
                }
gm_lb18:;
                uint8_t t108 = gm_l32;
                if (!t108) {
                    goto gm_lb14;
                }
                {
                    {
                        void * t109 = gm_l0;
                        void * t110 = gm_l1;
                        gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t110) + 72));
                        gm_tkn42u7na3t81 t112 = gm_f_type_of_tiqb21n(t109, GM_CONV(gm_cseahvg8v7p0vs, t111));
                        gm_l33 = t112;
                        uint8_t t113 = (uint8_t)((((uint8_t *)&gm_l33))[4] != 0);
                        if (!t113) {
                            goto gm_lb22;
                        }
                        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l33));
                        gm_l16 = t114;
                        void * t115 = gm_l0;
                        void * t116 = GM_LD(void *, ((uint8_t *)t115));
                        gm_t88rcucg1b7uk t117 = gm_l16;
                        gm_tf5srh4mp4b3i t118 = gm_f_adt_decl_of_2bflsov(t116, GM_CONV(gm_csguankko4lmhd, t117));
                        gm_l15 = t118;
                        goto gm_lb21;
                    }
gm_lb22:;
                }
gm_lb21:;
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
        gm_l18 = ((uint64_t)0ULL);
        uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_l17 = t119;
        {
            for (;;) {
                uint64_t t120 = gm_l18;
                uint64_t t121 = gm_l17;
                uint8_t t122 = (t120 < t121);
                if (!t122) {
                    goto gm_lb23;
                }
                {
                    {
                        {
                            uint64_t t123 = gm_l18;
                            gm_l19 = t123;
                            void * t124 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t126 = gm_l19;
                            void * t127 = (void *)(((uint8_t *)t124) + (uintptr_t)t126 * 144);
                            gm_l20 = t127;
                            {
                                {
                                    void * t128 = gm_l20;
                                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)t128) + 0));
                                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)1ULL));
                                    if (!t130) {
                                        goto gm_lb29;
                                    }
                                    memmove(((uint8_t *)&gm_l21), (((uint8_t *)t128) + 8), 32);
                                    {
                                        {
                                            uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
                                            if (!t131) {
                                                goto gm_lb31;
                                            }
                                            {
                                                memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l5), 24);
                                            }
                                            {
                                                void * t132 = gm_l0;
                                                uint64_t t133 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                                                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                                                gm_b0_1 t135 = gm_f_payload_field_3o532rn((void *)NULL, cx, NULL, t132, t133, t134, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb30;
                                        }
gm_lb31:;
                                        {
                                            {
                                                uint8_t t136 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                                                if (!t136) {
                                                    goto gm_lb33;
                                                }
                                                uint32_t t137 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                                                gm_l23 = t137;
                                                void * t138 = gm_l0;
                                                void * t139 = GM_LD(void *, ((uint8_t *)t138));
                                                void * t140 = GM_LD(void *, (((uint8_t *)t139) + 344));
                                                uint64_t t141 = GM_LD(uint64_t, ((((uint8_t *)t139) + 344) + 8));
                                                uint32_t t142 = gm_l23;
                                                uint64_t t143 = (uint64_t)t142;
                                                if ((uintptr_t)t143 >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)t143, t141, gm_srcb2hdor4f6g0q, 574, 26);
                                                gm_b16_8 t144 = GM_LD(gm_b16_8, ((((uint8_t *)t140) + (uintptr_t)t143 * 136) + 40));
                                                gm_l24 = t144;
                                                gm_l25 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t145 = gm_l25;
                                                        uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                        uint8_t t147 = (t145 < t146);
                                                        if (!t147) {
                                                            goto gm_lb34;
                                                        }
                                                        {
                                                            void * t148 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                            uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                            uint64_t t150 = gm_l25;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&F.l26), (((uint8_t *)t148) + (uintptr_t)t150 * 56), 56);
                                                                    gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                                                    uint8_t t153 = (gm_eq0iqiubfm7e(((uint8_t *)&t151), ((uint8_t *)&t152)));
                                                                    if (t153) {
                                                                        void * t154 = gm_l0;
                                                                        gm_tl874t2atnlh3 t155 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                                                        gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 20));
                                                                        gm_b0_1 t157 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t154, t155, t156);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        {
                                                                            void * t158 = gm_l0;
                                                                            gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 20));
                                                                            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                            void * t161 = gm_l0;
                                                                            void * t162 = GM_LD(void *, ((uint8_t *)t161));
                                                                            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 16));
                                                                            gm_b16_8 t164 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t162, t163);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_t6j6smj7qoctu t165;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t165 + 0), t160);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t165 + 16), t164);
                                                                            gm_f_format_f6rro2t((void *)&gm_l34, (void *)NULL, cx, NULL, &t165);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                                            if (!t166) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcb2hdor4f6g0q, 577, 56);
                                                                            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                                                            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
                                                                            gm_th2lqbo77pvsc t169;
                                                                            memset(&t169, 0, sizeof t169);
                                                                            gm_t6ok5tvcjc200 t170;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t168);
                                                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t170 + 16), t169);
                                                                            gm_b0_1 t171 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t158, t159, &t170);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb34;
                                                                    }
                                                                    goto gm_lb37;
                                                                }
gm_lb38:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 574, 17);
                                                            }
gm_lb37:;
                                                        }
gm_lb36:;
                                                        uint64_t t172 = gm_l25;
                                                        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
                                                        uint64_t t175 = r173;
                                                        gm_l25 = t175;
                                                    }
                                                }
gm_lb34:;
                                                goto gm_lb32;
                                            }
gm_lb33:;
                                            {
                                                {
                                                    uint8_t t176 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                    if (!t176) {
                                                        goto gm_lb40;
                                                    }
                                                    uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                    gm_l27 = t177;
                                                    {
                                                        {
                                                            void * t178 = gm_l0;
                                                            uint64_t t179 = gm_l27;
                                                            gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                                            gm_toceffb697p0u t181 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t178, t179, t180);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l35 = t181;
                                                            uint8_t t182 = (uint8_t)((((uint8_t *)&gm_l35))[12] != 0);
                                                            if (!t182) {
                                                                goto gm_lb42;
                                                            }
                                                            gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l35));
                                                            gm_l28 = t183;
                                                            void * t184 = gm_l0;
                                                            gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                                            gm_tl874t2atnlh3 t186 = gm_l28;
                                                            gm_b0_1 t187 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t184, t185, t186);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb41;
                                                        }
gm_lb42:;
                                                    }
gm_lb41:;
                                                    goto gm_lb39;
                                                }
gm_lb40:;
                                            }
gm_lb39:;
                                        }
gm_lb32:;
                                    }
gm_lb30:;
                                    goto gm_lb28;
                                }
gm_lb29:;
                            }
gm_lb28:;
                            void * t188 = gm_l0;
                            void * t189 = gm_l20;
                            gm_b0_1 t190 = gm_f_arg_amoajjv((void *)NULL, cx, NULL, t188, t189);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 568, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t191 = gm_l18;
                uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                uint64_t t194 = r192;
                gm_l18 = t194;
            }
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
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_classify_pdaj2gb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_classify_pdaj2gb F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* inner */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_tsncjtib3j1l1 gm_l4; /* d */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    uint64_t gm_l7; /* m */
    uint64_t gm_l8; /* d */
    void * gm_l9; /* callee */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 761u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    gm_toceffb697p0u gm_l12;
    gm_t1335qftqvd8f gm_l13;
    gm_tu0a56t4bfvqh gm_l14;
    gm_tf5srh4mp4b3i gm_l15;
    gm_tu0a56t4bfvqh gm_l16;
    void * t19 = gm_l1;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)9ULL));
            if (!t21) {
                goto gm_lb2;
            }
            void * t22 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l2 = t22;
            void * t23 = gm_l0;
            void * t24 = gm_l2;
            gm_tu0a56t4bfvqh t25 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)6ULL));
            if (!t27) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t19) + 8) + 8), 32);
            void * t28 = gm_l0;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_toceffb697p0u t30 = gm_f_lookup_local_tdg7skl(t28, t29);
            gm_l12 = t30;
            {
                {
                    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l12))[12] != 0);
                    if (t31) {
                        goto gm_lb5;
                    }
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 250, 16);
            }
gm_lb4:;
            uint8_t t32 = gm_l11;
            if (t32) {
                gm_tu0a56t4bfvqh t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
                F.ret = t33;
                goto gm_fret;
            }
            {
                {
                    {
                        void * t34 = gm_l0;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                        gm_f_lookup_decl_10pnl6c((void *)&gm_l13, (void *)NULL, cx, NULL, t34, t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
                    if (!t36) {
                        goto gm_lb8;
                    }
                    {
                        memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l13), 24);
                    }
                    {
                        void * t37 = gm_l0;
                        gm_tu0a56t4bfvqh t38 = gm_f_base_of_9tbrpvr((void *)NULL, cx, NULL, t37, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l4)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t38;
                        goto gm_fret;
                    }
                }
gm_lb8:;
            }
gm_lb7:;
            gm_tu0a56t4bfvqh t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)3ULL));
            F.ret = t39;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)22ULL));
            if (!t41) {
                goto gm_lb9;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l5 = t42;
            memmove(((uint8_t *)&gm_l6), (((((uint8_t *)t19) + 8) + 8) + 8), 32);
            {
                {
                    void * t43 = gm_l0;
                    void * t44 = gm_l5;
                    gm_tu0a56t4bfvqh t45 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t43, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t45;
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
                    if (!t47) {
                        goto gm_lb11;
                    }
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    gm_l7 = t48;
                    {
                        {
                            void * t49 = gm_l0;
                            void * t50 = GM_LD(void *, ((uint8_t *)t49));
                            void * t51 = GM_LD(void *, ((uint8_t *)t50));
                            uint64_t t52 = gm_l7;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                            gm_tf5srh4mp4b3i t54 = gm_f_lookup_59juqoo(t51, t52, t53);
                            gm_l15 = t54;
                            uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                            if (!t55) {
                                goto gm_lb13;
                            }
                            uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            gm_l8 = t56;
                            void * t57 = gm_l0;
                            uint64_t t58 = gm_l8;
                            gm_tsncjtib3j1l1 t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t59 + 16, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t59 + 8), t58);
                            gm_tu0a56t4bfvqh t60 = gm_f_base_of_9tbrpvr((void *)NULL, cx, NULL, t57, &t59);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t60;
                            goto gm_fret;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            gm_tu0a56t4bfvqh t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t61 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)3ULL));
            F.ret = t61;
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)19ULL));
            if (!t63) {
                goto gm_lb14;
            }
            void * t64 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l9 = t64;
            {
                {
                    void * t65 = gm_l0;
                    void * t66 = gm_l9;
                    gm_tu0a56t4bfvqh t67 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t67;
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)1ULL));
                    if (!t69) {
                        goto gm_lb16;
                    }
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    gm_l10 = t70;
                    uint64_t t71 = gm_l10;
                    gm_tu0a56t4bfvqh t72;
                    GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t72 + 8), t71);
                    F.ret = t72;
                    goto gm_fret;
                }
gm_lb16:;
            }
gm_lb15:;
            gm_tu0a56t4bfvqh t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t73 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)3ULL));
            F.ret = t73;
            goto gm_fret;
        }
gm_lb14:;
        {
            gm_tu0a56t4bfvqh t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)3ULL));
            F.ret = t74;
            goto gm_fret;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 247, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cond_d94lif5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_cond_d94lif5 F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l6; /* value */
    gm_tkn42u7na3t81 gm_l7; /* ty */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t10 = gm_l1;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 8));
    gm_l2 = t11;
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = gm_l2;
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t15 = gm_l3;
                        gm_l4 = t15;
                        void * t16 = gm_l1;
                        void * t17 = GM_LD(void *, ((uint8_t *)t16));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
                        uint64_t t19 = gm_l4;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcb2hdor4f6g0q, 668, 15);
                        {
                            {
                                uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t17) + (uintptr_t)t19 * 80) + 0));
                                uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l5), ((((uint8_t *)t17) + (uintptr_t)t19 * 80) + 8), 64);
                                void * t22 = GM_LD(void *, (((((uint8_t *)t17) + (uintptr_t)t19 * 80) + 8) + 64));
                                gm_l6 = t22;
                                void * t23 = gm_l0;
                                void * t24 = gm_l6;
                                gm_b0_1 t25 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t23, t24);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t26 = gm_l0;
                                void * t27 = gm_l6;
                                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
                                gm_tkn42u7na3t81 t29 = gm_f_type_of_tiqb21n(t26, GM_CONV(gm_cseahvg8v7p0vs, t28));
                                gm_l7 = t29;
                                void * t30 = gm_l0;
                                void * t31 = (void *)((uint8_t *)&F.l5);
                                gm_tkn42u7na3t81 t32 = gm_l7;
                                uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 48) + 8));
                                gm_b0_1 t34 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t30, t31, t32, t33);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t17) + (uintptr_t)t19 * 80) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                                if (!t36) {
                                    goto gm_lb8;
                                }
                                void * t37 = GM_LD(void *, ((((uint8_t *)t17) + (uintptr_t)t19 * 80) + 8));
                                gm_l8 = t37;
                                void * t38 = gm_l0;
                                void * t39 = gm_l8;
                                gm_b0_1 t40 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t38, t39);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 668, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 667, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l3;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l3 = t44;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_name_span_ht2nhb5 F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* x */
    uint32_t gm_l4; /* i */
    void * gm_l5; /* lit */
    GM_STACK_CHECK_SHALLOW(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l6;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            gm_l2 = t9;
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((uint8_t *)t10));
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 240));
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 240) + 8));
            uint64_t t15 = gm_l2;
            if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcb2hdor4f6g0q, 179, 28);
            gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t13) + (uintptr_t)t15 * 216) + 16));
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
            if (!t18) {
                goto gm_lb3;
            }
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            gm_l3 = t19;
            uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 8));
            gm_l4 = t20;
            {
                {
                    void * t21 = gm_l0;
                    void * t22 = GM_LD(void *, ((uint8_t *)t21));
                    uint64_t t23 = gm_l3;
                    void * t24 = gm_f_effect_lit_26tkk47(t22, t23);
                    gm_l6 = t24;
                    uint8_t t25 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
                    if (!t25) {
                        goto gm_lb5;
                    }
                    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    gm_l5 = t26;
                    void * t27 = gm_l5;
                    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 24));
                    uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 24) + 8));
                    uint32_t t30 = gm_l4;
                    uint64_t t31 = (uint64_t)t30;
                    if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srcb2hdor4f6g0q, 181, 51);
                    gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t28) + (uintptr_t)t31 * 80) + 16));
                    F.ret = t32;
                    goto gm_fret;
                }
gm_lb5:;
            }
gm_lb4:;
            gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            F.ret = t33;
            goto gm_fret;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 178, 5);
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5_c(void * a0, gm_tsncjtib3j1l1 a1) { return gm_f_decl_name_span_ht2nhb5(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_feg3ue0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, uint32_t a3) {
    struct gm_fr_declare_feg3ue0 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* id */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint32_t gm_l3; /* from */
    void * gm_l4; /* last */
    uint32_t gm_l5; /* end */
    gm_b16_8 gm_l6; /* code */
    gm_b16_8 gm_l7; /* detail */
    gm_tb59ptmmofnb5 gm_l8; /* f */
    gm_b16_8 gm_l9; /* shown */
    GM_STACK_CHECK(&F, 962u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tvsjp38cd9njc gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sfdbfrv46lihd));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
    uint8_t t6 = (gm_eq0iqiubfm7e(((uint8_t *)&t4), ((uint8_t *)&t5)));
    if (t6) {
        gm_l11 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 32) + 8));
        uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
        gm_l11 = t9;
    }
    uint8_t t10 = gm_l11;
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 32));
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 32) + 8));
    void * t14 = gm_l0;
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t14) + 32) + 8));
    uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 155, 29);
    uint64_t t18 = r16;
    if ((uintptr_t)t18 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t18, t13, gm_srcb2hdor4f6g0q, 155, 14);
    void * t19 = (void *)(((uint8_t *)t12) + (uintptr_t)t18 * 48);
    gm_l4 = t19;
    void * t20 = gm_l4;
    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t20) + 40));
    gm_l5 = t21;
    {
        void * t22 = gm_l4;
        void * t23 = (void *)((uint8_t *)t22);
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_t08olq97rscn3 t26;
        GM_ST(uint32_t, (uint8_t *)&t26 + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t24);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t26 + 16), t25);
        gm_tftb1odc739t5 t27 = gm_f_push_bb3f68j(t23, &t26);
        gm_l12 = t27;
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcb2hdor4f6g0q, 157, 5);
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_l6 = t29;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(0));
    gm_l7 = t30;
    {
        {
            {
                void * t31 = gm_l0;
                void * t32 = GM_LD(void *, (((uint8_t *)t31) + 8));
                void * t33 = (void *)(((uint8_t *)t32) + 168);
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
                gm_f_get_shqk64n((void *)&gm_l13, t33, GM_CONV(gm_cseahvg8v7p0vs, t34));
            }
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
            if (!t35) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l13), 24);
            }
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, ((uint8_t *)t36));
            gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 16));
            gm_b16_8 t39 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t39;
            {
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                gm_b16_8 t41 = gm_l9;
                gm_t6j6smj7qoctu t42;
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t40);
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 16), t41);
                gm_f_format_f6rro2t((void *)&gm_l14, (void *)NULL, cx, NULL, &t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcb2hdor4f6g0q, 162, 16);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_l6 = t45;
            gm_b16_8 t46 = gm_l9;
            gm_l7 = t46;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t47 = gm_l0;
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_b16_8 t49 = gm_l6;
        gm_th2lqbo77pvsc t50;
        memset(&t50, 0, sizeof t50);
        gm_t6ok5tvcjc200 t51;
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t49);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t51 + 16), t50);
        gm_b0_1 t52 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t47, t48, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_t9qkh0rsooc8d t54 = gm_l2;
        gm_b16_8 t55 = gm_l7;
        gm_th2lqbo77pvsc t56;
        memset(&t56, 0, sizeof t56);
        GM_ST(uint8_t, (uint8_t *)&F.l10 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l10 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l10 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 0), t53);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l10 + 16), t54);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 24), t55);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l10 + 40), t56);
    }
    {
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 16));
        void * t59 = (void *)(((uint8_t *)t58) + 136);
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        uint32_t t61 = gm_l3;
        uint32_t t62 = gm_l5;
        gm_tl5cuhjfnc4cr t63 = GM_LD(gm_tl5cuhjfnc4cr, ((uint8_t *)&F.l10));
        gm_tfgiovingnnea t64;
        GM_ST(uint32_t, (uint8_t *)&t64 + 84, 0);
        GM_ST(gm_tl5cuhjfnc4cr, ((uint8_t *)&t64 + 0), t63);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t64 + 64), t60);
        GM_ST(uint32_t, ((uint8_t *)&t64 + 76), t61);
        GM_ST(uint32_t, ((uint8_t *)&t64 + 80), t62);
        gm_tftb1odc739t5 t65 = gm_f_push_sige3eu(t59, &t64);
        gm_l15 = t65;
    }
    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcb2hdor4f6g0q, 167, 5);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_effect_lit_26tkk47(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_lit_26tkk47 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    void * gm_l4; /* lit */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 240));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 240) + 8));
    uint64_t t10 = gm_l1;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_srcb2hdor4f6g0q, 74, 14);
    void * t11 = (void *)(((uint8_t *)t8) + (uintptr_t)t10 * 216);
    gm_l2 = t11;
    {
        {
            void * t12 = gm_l2;
            uint8_t t13 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 64) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)t12) + 40) + 64) + 8), 64);
            {
                {
                    void * t15 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)34ULL));
                    if (!t17) {
                        goto gm_lb4;
                    }
                    void * t18 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
                    gm_l4 = t18;
                    void * t19 = gm_l4;
                    F.ret = t19;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((void *)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ensure_hover_3epdjmk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_ensure_hover_3epdjmk F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* key */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* x */
    uint64_t gm_l6; /* x */
    uint32_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tftb1odc739t5 gm_l9;
    {
        uint64_t t5 = gm_f_ref_key_vam5ppg(&(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l1)));
        gm_l2 = t5;
    }
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 112);
    uint64_t t8 = gm_l2;
    uint8_t t9 = gm_f_contains_475f687(t7, t8);
    if (t9) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 112);
    uint64_t t12 = gm_l2;
    gm_tftb1odc739t5 t13 = gm_f_put_1rbrft2(t11, t12, ((uint8_t)1));
    gm_l9 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcb2hdor4f6g0q, 190, 5);
    {
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = GM_CONV(gm_tl874t2atnlh3, gm_f_decl_name_span_ht2nhb5(t15, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l1))));
        gm_l3 = t16;
    }
    gm_tl874t2atnlh3 t17 = gm_l3;
    gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t19 = (gm_eqff85ac10o3(((uint8_t *)&t17), ((uint8_t *)&t18)));
    if (t19) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            gm_l4 = t22;
            {
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, ((uint8_t *)t23));
                uint64_t t25 = gm_l4;
                gm_f_decl_info_ahk1bni((void *)&F.l5, (void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t26 = gm_l0;
                gm_tl874t2atnlh3 t27 = gm_l3;
                gm_b0_1 t28 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t26, t27, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l5) + 8)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)1ULL));
            if (!t30) {
                goto gm_lb3;
            }
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            gm_l6 = t31;
            uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 8) + 8));
            gm_l7 = t32;
            {
                void * t33 = gm_l0;
                void * t34 = GM_LD(void *, ((uint8_t *)t33));
                uint64_t t35 = gm_l6;
                uint32_t t36 = gm_l7;
                gm_f_op_info_4a09t36((void *)&F.l8, (void *)NULL, cx, NULL, t34, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t37 = gm_l0;
                gm_tl874t2atnlh3 t38 = gm_l3;
                gm_b0_1 t39 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t37, t38, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l8) + 8)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 193, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_kih3eo8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_kih3eo8 F;
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t08olq97rscn3 gm_l6; /* c */
    void * gm_l7; /* base */
    gm_t08olq97rscn3 gm_l8; /* name */
    void * gm_l9; /* callee */
    gm_b16_8 gm_l10; /* args */
    void * gm_l11; /* sg */
    void * gm_l12; /* lit */
    void * gm_l13; /* h */
    void * gm_l14; /* f */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l19; /* ty */
    void * gm_l20; /* i */
    void * gm_l21; /* m */
    void * gm_l22; /* x */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* on */
    void * gm_l27; /* f */
    void * gm_l30; /* v */
    GM_STACK_CHECK(&F, 1276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t77 = gm_l1;
        {
            {
                {
                    {
                        {
                            uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)0ULL));
                            if (!t79) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)1ULL));
                            if (!t81) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)2ULL));
                            if (!t83) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)3ULL));
                            if (!t85) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)4ULL));
                            if (!t87) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)5ULL));
                            if (!t89) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)7ULL));
                            if (!t91) {
                                goto gm_lb10;
                            }
                            goto gm_lb3;
                        }
gm_lb10:;
                        {
                            uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)50ULL));
                            if (!t93) {
                                goto gm_lb11;
                            }
                            goto gm_lb3;
                        }
gm_lb11:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)47ULL));
                    if (!t95) {
                        goto gm_lb12;
                    }
                    {
                        memmove(((uint8_t *)&F.l2), ((((uint8_t *)t77) + 8) + 8), 40);
                    }
                    {
                        void * t96 = F.l0;
                        gm_b0_1 t97 = gm_f_label_use_9lcr9t0((void *)NULL, cx, NULL, t96, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)6ULL));
                    if (!t99) {
                        goto gm_lb13;
                    }
                    {
                        memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t77) + 8) + 8), 32);
                    }
                    {
                        void * t100 = F.l0;
                        gm_b0_1 t101 = gm_f_name_use_73pph7t((void *)NULL, cx, NULL, t100, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)8ULL));
                    if (!t103) {
                        goto gm_lb14;
                    }
                    {
                        memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t77) + 8) + 8), 32);
                    }
                    {
                        void * t104 = F.l0;
                        void * t105 = gm_l1;
                        gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 72));
                        gm_tf5srh4mp4b3i t107 = gm_f_variant_at_9i78oa0((void *)NULL, cx, NULL, t104, t106, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb14:;
            }
            {
                {
                    {
                        {
                            uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)9ULL));
                            if (!t109) {
                                goto gm_lb17;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)11ULL));
                            if (!t111) {
                                goto gm_lb18;
                            }
                            goto gm_lb16;
                        }
gm_lb18:;
                        {
                            uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t113 = (uint8_t)(t112 == ((uint8_t)15ULL));
                            if (!t113) {
                                goto gm_lb19;
                            }
                            goto gm_lb16;
                        }
gm_lb19:;
                        {
                            uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)16ULL));
                            if (!t115) {
                                goto gm_lb20;
                            }
                            goto gm_lb16;
                        }
gm_lb20:;
                        {
                            uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)18ULL));
                            if (!t117) {
                                goto gm_lb21;
                            }
                            goto gm_lb16;
                        }
gm_lb21:;
                        {
                            uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t119 = (uint8_t)(t118 == ((uint8_t)24ULL));
                            if (!t119) {
                                goto gm_lb22;
                            }
                            goto gm_lb16;
                        }
gm_lb22:;
                        {
                            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t121 = (uint8_t)(t120 == ((uint8_t)31ULL));
                            if (!t121) {
                                goto gm_lb23;
                            }
                            goto gm_lb16;
                        }
gm_lb23:;
                        {
                            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t123 = (uint8_t)(t122 == ((uint8_t)12ULL));
                            if (!t123) {
                                goto gm_lb24;
                            }
                            goto gm_lb16;
                        }
gm_lb24:;
                        {
                            uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t125 = (uint8_t)(t124 == ((uint8_t)25ULL));
                            if (!t125) {
                                goto gm_lb25;
                            }
                            goto gm_lb16;
                        }
gm_lb25:;
                        {
                            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)26ULL));
                            if (!t127) {
                                goto gm_lb26;
                            }
                            goto gm_lb16;
                        }
gm_lb26:;
                        {
                            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)27ULL));
                            if (!t129) {
                                goto gm_lb27;
                            }
                            goto gm_lb16;
                        }
gm_lb27:;
                        {
                            uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t131 = (uint8_t)(t130 == ((uint8_t)35ULL));
                            if (!t131) {
                                goto gm_lb28;
                            }
                            goto gm_lb16;
                        }
gm_lb28:;
                        {
                            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t133 = (uint8_t)(t132 == ((uint8_t)10ULL));
                            if (!t133) {
                                goto gm_lb29;
                            }
                            goto gm_lb16;
                        }
gm_lb29:;
                        {
                            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t135 = (uint8_t)(t134 == ((uint8_t)13ULL));
                            if (!t135) {
                                goto gm_lb30;
                            }
                            goto gm_lb16;
                        }
gm_lb30:;
                        {
                            uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t137 = (uint8_t)(t136 == ((uint8_t)17ULL));
                            if (!t137) {
                                goto gm_lb31;
                            }
                            goto gm_lb16;
                        }
gm_lb31:;
                        {
                            uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t139 = (uint8_t)(t138 == ((uint8_t)29ULL));
                            if (!t139) {
                                goto gm_lb32;
                            }
                            goto gm_lb16;
                        }
gm_lb32:;
                        {
                            uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)28ULL));
                            if (!t141) {
                                goto gm_lb33;
                            }
                            goto gm_lb16;
                        }
gm_lb33:;
                        {
                            uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t143 = (uint8_t)(t142 == ((uint8_t)20ULL));
                            if (!t143) {
                                goto gm_lb34;
                            }
                            goto gm_lb16;
                        }
gm_lb34:;
                        {
                            uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t145 = (uint8_t)(t144 == ((uint8_t)21ULL));
                            if (!t145) {
                                goto gm_lb35;
                            }
                            goto gm_lb16;
                        }
gm_lb35:;
                        {
                            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)23ULL));
                            if (!t147) {
                                goto gm_lb36;
                            }
                            goto gm_lb16;
                        }
gm_lb36:;
                        {
                            uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t149 = (uint8_t)(t148 == ((uint8_t)48ULL));
                            if (!t149) {
                                goto gm_lb37;
                            }
                            goto gm_lb16;
                        }
gm_lb37:;
                        {
                            uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t151 = (uint8_t)(t150 == ((uint8_t)45ULL));
                            if (!t151) {
                                goto gm_lb38;
                            }
                            goto gm_lb16;
                        }
gm_lb38:;
                        {
                            uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                            uint8_t t153 = (uint8_t)(t152 == ((uint8_t)49ULL));
                            if (!t153) {
                                goto gm_lb39;
                            }
                            goto gm_lb16;
                        }
gm_lb39:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    void * t154 = gm_l1;
                    gm_closure t155 = ((gm_closure){(void *)gm_f_fn_rqpk5og, (void *)&F.h, cx});
                    gm_f_for_each_child_tkrfoqu(t154, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t157 = (uint8_t)(t156 == ((uint8_t)14ULL));
                    if (!t157) {
                        goto gm_lb40;
                    }
                    memmove(((uint8_t *)&F.l5), ((((uint8_t *)t77) + 8) + 8), 56);
                    void * t158 = F.l0;
                    void * t159 = GM_LD(void *, ((uint8_t *)&F.l5));
                    gm_b0_1 t160 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t158, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t161 = F.l0;
                    void * t162 = GM_LD(void *, (((uint8_t *)&F.l5) + 48));
                    uint32_t t163 = GM_LD(uint32_t, ((((uint8_t *)t162) + 72) + 8));
                    gm_b0_1 t164 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t161, t163);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t165 = (uint8_t)(((((uint8_t *)&F.l5) + 8))[32] != 0);
                            if (!t165) {
                                goto gm_lb42;
                            }
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)&F.l5) + 8), 32);
                            }
                            {
                                void * t166 = F.l0;
                                uint32_t t167 = GM_LD(uint32_t, ((((uint8_t *)&gm_l6) + 16) + 8));
                                gm_t9qkh0rsooc8d t168;
                                GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)8ULL));
                                gm_b0_1 t169 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t166, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l6)), t168, t167);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb41;
                        }
gm_lb42:;
                    }
gm_lb41:;
                    void * t170 = F.l0;
                    void * t171 = GM_LD(void *, (((uint8_t *)&F.l5) + 48));
                    gm_b0_1 t172 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t173 = F.l0;
                    gm_f_pop_scope_vgka5ae(t173);
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)22ULL));
                    if (!t175) {
                        goto gm_lb43;
                    }
                    void * t176 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l7 = t176;
                    {
                        memmove(((uint8_t *)&gm_l8), (((((uint8_t *)t77) + 8) + 8) + 8), 32);
                    }
                    {
                        void * t177 = F.l0;
                        void * t178 = gm_l1;
                        void * t179 = gm_l7;
                        gm_b0_1 t180 = gm_f_field_7p9pmjo((void *)NULL, cx, NULL, t177, t178, t179, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)19ULL));
                    if (!t182) {
                        goto gm_lb44;
                    }
                    void * t183 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l9 = t183;
                    gm_b16_8 t184 = GM_LD(gm_b16_8, (((((uint8_t *)t77) + 8) + 8) + 8));
                    gm_l10 = t184;
                    void * t185 = F.l0;
                    void * t186 = gm_l1;
                    void * t187 = gm_l9;
                    gm_b16_8 t188 = gm_l10;
                    gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                    gm_b0_1 t190 = gm_f_call_8vfr61j((void *)NULL, cx, NULL, t185, t186, t187, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb44:;
                {
                    uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)30ULL));
                    if (!t192) {
                        goto gm_lb45;
                    }
                    void * t193 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l11 = t193;
                    void * t194 = F.l0;
                    void * t195 = gm_l11;
                    gm_b0_1 t196 = gm_f_sig_89f514c((void *)NULL, cx, NULL, t194, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb45:;
            }
            {
                {
                    uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t198 = (uint8_t)(t197 == ((uint8_t)32ULL));
                    if (!t198) {
                        goto gm_lb46;
                    }
                    void * t199 = F.l0;
                    void * t200 = gm_l1;
                    gm_b0_1 t201 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t199, t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb46:;
                {
                    uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t203 = (uint8_t)(t202 == ((uint8_t)33ULL));
                    if (!t203) {
                        goto gm_lb47;
                    }
                    void * t204 = F.l0;
                    void * t205 = gm_l1;
                    gm_b0_1 t206 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t204, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb47:;
                {
                    uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)34ULL));
                    if (!t208) {
                        goto gm_lb48;
                    }
                    void * t209 = F.l0;
                    void * t210 = gm_l1;
                    gm_b0_1 t211 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t209, t210);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)36ULL));
                    if (!t213) {
                        goto gm_lb49;
                    }
                    void * t214 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l12 = t214;
                    void * t215 = F.l0;
                    void * t216 = gm_l12;
                    gm_b0_1 t217 = gm_f_fn_literal_sodo3l0((void *)NULL, cx, NULL, t215, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb49:;
                {
                    uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)37ULL));
                    if (!t219) {
                        goto gm_lb50;
                    }
                    void * t220 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l13 = t220;
                    void * t221 = F.l0;
                    void * t222 = gm_l13;
                    gm_b0_1 t223 = gm_f_handler_literal_bbdui98((void *)NULL, cx, NULL, t221, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb50:;
                {
                    uint8_t t224 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t225 = (uint8_t)(t224 == ((uint8_t)38ULL));
                    if (!t225) {
                        goto gm_lb51;
                    }
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t227 = (uint8_t)(t226 == ((uint8_t)39ULL));
                    if (!t227) {
                        goto gm_lb52;
                    }
                    void * t228 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l14 = t228;
                    gm_l16 = ((uint64_t)0ULL);
                    void * t229 = gm_l14;
                    uint64_t t230 = GM_LD(uint64_t, ((((uint8_t *)t229) + 24) + 8));
                    gm_l15 = t230;
                    {
                        for (;;) {
                            uint64_t t232 = gm_l16;
                            uint64_t t233 = gm_l15;
                            uint8_t t234 = (t232 < t233);
                            if (!t234) {
                                goto gm_lb53;
                            }
                            {
                                {
                                    {
                                        uint64_t t235 = gm_l16;
                                        gm_l17 = t235;
                                        void * t236 = gm_l14;
                                        void * t237 = GM_LD(void *, (((uint8_t *)t236) + 24));
                                        uint64_t t238 = GM_LD(uint64_t, ((((uint8_t *)t236) + 24) + 8));
                                        uint64_t t239 = gm_l17;
                                        if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_srcb2hdor4f6g0q, 464, 23);
                                        {
                                            {
                                                uint8_t t240 = GM_LD(uint8_t, (((((uint8_t *)t237) + (uintptr_t)t239 * 144) + 16) + 0));
                                                uint8_t t241 = (uint8_t)(t240 == ((uint8_t)0ULL));
                                                if (!t241) {
                                                    goto gm_lb59;
                                                }
                                                memmove(((uint8_t *)&F.l18), ((((((uint8_t *)t237) + (uintptr_t)t239 * 144) + 16) + 8) + 32), 72);
                                                void * t242 = F.l0;
                                                void * t243 = (void *)((uint8_t *)&F.l18);
                                                gm_b0_1 t244 = gm_f_sig_89f514c((void *)NULL, cx, NULL, t242, t243);
                                                if (gm_unw.kind) {
                                                    goto gm_ll231;
                                                }
                                                goto gm_lb58;
                                            }
gm_lb59:;
                                            {
                                                uint8_t t245 = GM_LD(uint8_t, (((((uint8_t *)t237) + (uintptr_t)t239 * 144) + 16) + 0));
                                                uint8_t t246 = (uint8_t)(t245 == ((uint8_t)1ULL));
                                                if (!t246) {
                                                    goto gm_lb60;
                                                }
                                                void * t247 = GM_LD(void *, ((((((uint8_t *)t237) + (uintptr_t)t239 * 144) + 16) + 8) + 32));
                                                gm_l19 = t247;
                                                void * t248 = F.l0;
                                                void * t249 = gm_l19;
                                                gm_b0_1 t250 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t248, t249);
                                                if (gm_unw.kind) {
                                                    goto gm_ll231;
                                                }
                                                goto gm_lb58;
                                            }
gm_lb60:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 464, 17);
                                        }
gm_lb58:;
                                        goto gm_lb56;
                                    }
gm_lb57:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 463, 13);
                                }
gm_lb56:;
                            }
gm_lb55:;
                            uint64_t t251 = gm_l16;
                            uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                            uint64_t t254 = r252;
                            gm_l16 = t254;
                        }
                    }
                    goto gm_ls255;
gm_ll231:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb53; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb55; }
                    }
                    goto gm_lad;
gm_ls255:;
gm_lb53:;
                    goto gm_lb1;
                }
gm_lb52:;
            }
            {
                {
                    uint8_t t256 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t257 = (uint8_t)(t256 == ((uint8_t)40ULL));
                    if (!t257) {
                        goto gm_lb61;
                    }
                    void * t258 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l20 = t258;
                    void * t259 = F.l0;
                    void * t260 = gm_l20;
                    gm_b0_1 t261 = gm_f_if_expr_6oqub48((void *)NULL, cx, NULL, t259, t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb61:;
                {
                    uint8_t t262 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t263 = (uint8_t)(t262 == ((uint8_t)41ULL));
                    if (!t263) {
                        goto gm_lb62;
                    }
                    void * t264 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l21 = t264;
                    void * t265 = F.l0;
                    void * t266 = gm_l21;
                    gm_b0_1 t267 = gm_f_match_expr_j2bvhsh((void *)NULL, cx, NULL, t265, t266);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb62:;
                {
                    uint8_t t268 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)42ULL));
                    if (!t269) {
                        goto gm_lb63;
                    }
                    void * t270 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l22 = t270;
                    gm_l24 = ((uint64_t)0ULL);
                    void * t271 = gm_l22;
                    uint64_t t272 = GM_LD(uint64_t, ((((uint8_t *)t271) + 40) + 8));
                    gm_l23 = t272;
                    {
                        for (;;) {
                            uint64_t t274 = gm_l24;
                            uint64_t t275 = gm_l23;
                            uint8_t t276 = (t274 < t275);
                            if (!t276) {
                                goto gm_lb64;
                            }
                            {
                                {
                                    {
                                        uint64_t t277 = gm_l24;
                                        gm_l25 = t277;
                                        void * t278 = F.l0;
                                        void * t279 = gm_l22;
                                        void * t280 = GM_LD(void *, (((uint8_t *)t279) + 40));
                                        uint64_t t281 = GM_LD(uint64_t, ((((uint8_t *)t279) + 40) + 8));
                                        uint64_t t282 = gm_l25;
                                        if ((uintptr_t)t282 >= (uintptr_t)(t281)) gm_trap_index((uintptr_t)t282, t281, gm_srcb2hdor4f6g0q, 473, 50);
                                        void * t283 = (void *)(((uint8_t *)t280) + (uintptr_t)t282 * 88);
                                        gm_b0_1 t284 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t278, t283);
                                        if (gm_unw.kind) {
                                            goto gm_ll273;
                                        }
                                        goto gm_lb67;
                                    }
gm_lb68:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 473, 13);
                                }
gm_lb67:;
                            }
gm_lb66:;
                            uint64_t t285 = gm_l24;
                            uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                            uint64_t t288 = r286;
                            gm_l24 = t288;
                        }
                    }
                    goto gm_ls289;
gm_ll273:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb64; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb66; }
                    }
                    goto gm_lad;
gm_ls289:;
gm_lb64:;
                    {
                        {
                            void * t290 = gm_l22;
                            uint8_t t291 = (uint8_t)(GM_LD(void *, (((uint8_t *)t290) + 56)) != 0);
                            if (!t291) {
                                goto gm_lb70;
                            }
                            void * t292 = GM_LD(void *, (((uint8_t *)t290) + 56));
                            gm_l26 = t292;
                            void * t293 = F.l0;
                            void * t294 = gm_l26;
                            gm_b0_1 t295 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t293, t294);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb69;
                        }
gm_lb70:;
                    }
gm_lb69:;
                    {
                        void * t296 = F.l0;
                        void * t297 = gm_l22;
                        gm_b0_1 t298 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t296, &(*(gm_t1s75i08qtptd *)((uint8_t *)t297)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t299 = F.l0;
                    void * t300 = gm_l22;
                    void * t301 = (void *)(((uint8_t *)t300) + 64);
                    gm_b0_1 t302 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t299, t301);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t303 = F.l0;
                        void * t304 = gm_l22;
                        gm_f_pop_label_e8oj3h3(t303, &(*(gm_t1s75i08qtptd *)((uint8_t *)t304)));
                    }
                    goto gm_lb1;
                }
gm_lb63:;
            }
            {
                {
                    uint8_t t305 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t306 = (uint8_t)(t305 == ((uint8_t)43ULL));
                    if (!t306) {
                        goto gm_lb71;
                    }
                    void * t307 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
                    gm_l27 = t307;
                    void * t308 = F.l0;
                    void * t309 = gm_l27;
                    gm_b0_1 t310 = gm_f_for_expr_f16jm2b((void *)NULL, cx, NULL, t308, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb71:;
                {
                    uint8_t t311 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t312 = (uint8_t)(t311 == ((uint8_t)44ULL));
                    if (!t312) {
                        goto gm_lb72;
                    }
                    memmove(((uint8_t *)&F.l28), ((((uint8_t *)t77) + 8) + 8), 40);
                    void * t313 = F.l0;
                    void * t314 = (void *)((uint8_t *)&F.l28);
                    gm_b0_1 t315 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t313, t314);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb72:;
                {
                    uint8_t t316 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                    uint8_t t317 = (uint8_t)(t316 == ((uint8_t)46ULL));
                    if (!t317) {
                        goto gm_lb73;
                    }
                    {
                        memmove(((uint8_t *)&F.l29), ((((uint8_t *)t77) + 8) + 8), 48);
                    }
                    {
                        void * t318 = F.l0;
                        gm_b0_1 t319 = gm_f_label_use_9lcr9t0((void *)NULL, cx, NULL, t318, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l29)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t320 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l29) + 40)) != 0);
                            if (!t320) {
                                goto gm_lb75;
                            }
                            void * t321 = GM_LD(void *, (((uint8_t *)&F.l29) + 40));
                            gm_l30 = t321;
                            void * t322 = F.l0;
                            void * t323 = gm_l30;
                            gm_b0_1 t324 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t322, t323);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb74;
                        }
gm_lb75:;
                    }
gm_lb74:;
                    goto gm_lb1;
                }
gm_lb73:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 437, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_field_7p9pmjo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3) {
    struct gm_fr_field_7p9pmjo F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    gm_t08olq97rscn3 gm_l3; /* name */
    uint64_t gm_l4; /* m */
    uint64_t gm_l5; /* d */
    uint64_t gm_l6; /* d */
    gm_tl874t2atnlh3 gm_l7; /* to */
    uint64_t gm_l8; /* d */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tu0a56t4bfvqh gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    gm_toceffb697p0u gm_l12;
    gm_tkn42u7na3t81 gm_l13;
    void * t13 = gm_l0;
    void * t14 = gm_l2;
    gm_tu0a56t4bfvqh t15 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t15;
    {
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
            if (!t17) {
                goto gm_lb2;
            }
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            gm_l4 = t18;
            void * t19 = gm_l0;
            void * t20 = gm_l2;
            gm_b0_1 t21 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    void * t22 = gm_l0;
                    void * t23 = GM_LD(void *, ((uint8_t *)t22));
                    void * t24 = GM_LD(void *, ((uint8_t *)t23));
                    uint64_t t25 = gm_l4;
                    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)a3));
                    gm_tf5srh4mp4b3i t27 = gm_f_lookup_59juqoo(t24, t25, t26);
                    gm_l11 = t27;
                    uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                    if (!t28) {
                        goto gm_lb4;
                    }
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                    gm_l5 = t29;
                    {
                        void * t30 = gm_l0;
                        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                        uint64_t t32 = gm_l5;
                        gm_tsncjtib3j1l1 t33;
                        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t33 + 8), t32);
                        gm_b0_1 t34 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t30, t31, &t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t35 = gm_l0;
                        void * t36 = gm_l1;
                        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
                        uint64_t t38 = gm_l5;
                        gm_tsncjtib3j1l1 t39;
                        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
                        gm_b0_1 t40 = gm_f_note_receiver_6mb4kc5((void *)NULL, cx, NULL, t35, t37, &t39);
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
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)1ULL));
            if (!t42) {
                goto gm_lb5;
            }
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            gm_l6 = t43;
            void * t44 = gm_l0;
            void * t45 = gm_l2;
            gm_b0_1 t46 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    void * t47 = gm_l0;
                    uint64_t t48 = gm_l6;
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)a3));
                    gm_toceffb697p0u t50 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t47, t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t50;
                    uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l12))[12] != 0);
                    if (!t51) {
                        goto gm_lb7;
                    }
                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
                    gm_l7 = t52;
                    void * t53 = gm_l0;
                    gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                    gm_tl874t2atnlh3 t55 = gm_l7;
                    gm_b0_1 t56 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t53, t54, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)2ULL));
            if (!t58) {
                goto gm_lb8;
            }
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            gm_l8 = t59;
            void * t60 = gm_l0;
            void * t61 = gm_l2;
            gm_b0_1 t62 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t63 = gm_l0;
            uint64_t t64 = gm_l8;
            gm_b0_1 t65 = gm_f_operation_use_ue1or58((void *)NULL, cx, NULL, t63, t64, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)3ULL));
            if (!t67) {
                goto gm_lb9;
            }
            void * t68 = gm_l0;
            void * t69 = gm_l2;
            gm_b0_1 t70 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    void * t71 = gm_l0;
                    void * t72 = gm_l2;
                    gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
                    gm_tkn42u7na3t81 t74 = gm_f_type_of_tiqb21n(t71, GM_CONV(gm_cseahvg8v7p0vs, t73));
                    gm_l13 = t74;
                    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
                    if (!t75) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
                    gm_l9 = t76;
                    void * t77 = gm_l0;
                    gm_t88rcucg1b7uk t78 = gm_l9;
                    uint8_t t79 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t77, t78, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 498, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_field_use_838f8e8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_field_use_838f8e8 F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    gm_tl874t2atnlh3 gm_l4; /* to */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint32_t gm_l7; /* id */
    void * gm_l8; /* info */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l10;
    gm_toceffb697p0u gm_l11;
    gm_t53at80pfdus3 gm_l12;
    gm_t53at80pfdus3 gm_l13;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    gm_t88rcucg1b7uk t13 = gm_l1;
    gm_tf5srh4mp4b3i t14 = gm_f_adt_decl_of_2bflsov(t12, GM_CONV(gm_csguankko4lmhd, t13));
    gm_l10 = t14;
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
        gm_l3 = t16;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t17 = gm_l0;
            uint64_t t18 = gm_l3;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_toceffb697p0u t20 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t20;
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l11))[12] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l11));
            gm_l4 = t22;
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_tl874t2atnlh3 t25 = gm_l4;
            gm_b0_1 t26 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t88rcucg1b7uk t27 = gm_l1;
    gm_l5 = t27;
    {
        for (;;) {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, ((uint8_t *)t28));
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            gm_t88rcucg1b7uk t31 = gm_l5;
            gm_f_kind_jcnt4hm((void *)&gm_l12, t30, GM_CONV(gm_csguankko4lmhd, t31));
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)8ULL));
            if (!t33) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l12) + 8) + 4));
            gm_l6 = t34;
            {
                gm_t88rcucg1b7uk t35 = gm_l6;
                gm_l5 = t35;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    {
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, ((uint8_t *)t36));
            void * t38 = (void *)(((uint8_t *)t37) + 8);
            gm_t88rcucg1b7uk t39 = gm_l5;
            gm_f_kind_jcnt4hm((void *)&gm_l13, t38, GM_CONV(gm_csguankko4lmhd, t39));
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)17ULL));
            if (!t41) {
                goto gm_lb7;
            }
            uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
            gm_l7 = t42;
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, ((uint8_t *)t43));
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 1088));
            uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)t44) + 1088) + 8));
            uint32_t t47 = gm_l7;
            uint64_t t48 = (uint64_t)t47;
            if ((uintptr_t)t48 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t48, t46, gm_srcb2hdor4f6g0q, 380, 18);
            void * t49 = (void *)(((uint8_t *)t45) + (uintptr_t)t48 * 184);
            gm_l8 = t49;
            void * t50 = gm_l8;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 112));
            if (t51) {
                {
                    {
                        void * t52 = gm_l8;
                        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 24) + 0));
                        uint8_t t54 = (uint8_t)(t53 == ((uint8_t)0ULL));
                        if (!t54) {
                            goto gm_lb9;
                        }
                        memmove(((uint8_t *)&F.l9), ((((uint8_t *)t52) + 24) + 8), 48);
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        uint8_t t56 = (t55 > ((uint64_t)0ULL));
                        if (t56) {
                            void * t57 = gm_l0;
                            void * t58 = GM_LD(void *, ((uint8_t *)&F.l9));
                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t58) + (uintptr_t)((uint64_t)0ULL) * 40) + 16));
                            uint8_t t61 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t57, t60, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t61;
                            goto gm_fret;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
            }
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    F.ret = ((uint8_t)0);
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
__attribute__((aligned(4))) void gm_f_find_field_l0p9325(gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_field_l0p9325 F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* name */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 551u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l12;
    gm_b16_8 t20 = gm_l0;
    gm_l3 = t20;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l4;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t26 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t24) + (uintptr_t)t26 * 80), 80);
                        {
                            {
                                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
                                if (!t28) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l5) + 8), 72);
                                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
                                gm_l7 = t29;
                                gm_l8 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t30 = gm_l8;
                                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint8_t t32 = (t30 < t31);
                                        if (!t32) {
                                            goto gm_lb8;
                                        }
                                        {
                                            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                            uint64_t t35 = gm_l8;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l9), (((uint8_t *)t33) + (uintptr_t)t35 * 32), 32);
                                                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                                                    gm_b16_8 t37 = gm_l1;
                                                    uint8_t t38 = (gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
                                                    if (t38) {
                                                        void * t39 = gm_l2;
                                                        {
                                                            {
                                                                uint8_t t40 = (uint8_t)((((uint8_t *)t39))[28] != 0);
                                                                if (t40) {
                                                                    goto gm_lb14;
                                                                }
                                                                gm_l12 = ((uint8_t)1);
                                                                goto gm_lb13;
                                                            }
gm_lb14:;
                                                            {
                                                                gm_l12 = ((uint8_t)0);
                                                                goto gm_lb13;
                                                            }
gm_lb15:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 288, 28);
                                                        }
gm_lb13:;
                                                        uint8_t t41 = gm_l12;
                                                        if (t41) {
                                                            void * t42 = gm_l2;
                                                            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                                                            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                                                            gm_tt7dbdq4n1egc t45;
                                                            GM_ST(uint8_t, (uint8_t *)&t45 + 25, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t45 + 26, 0);
                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t45 + 0), t43);
                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t45 + 12), t44);
                                                            GM_ST(uint8_t, ((uint8_t *)&t45 + 24), ((uint8_t)0));
                                                            gm_tkol5reu9t1u2 t46;
                                                            GM_ST(uint8_t, (uint8_t *)&t46 + 29, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t46 + 30, 0);
                                                            GM_ST(gm_tt7dbdq4n1egc, ((uint8_t *)&t46), t45);
                                                            ((uint8_t *)&t46)[28] = 1;
                                                            GM_ST(gm_tkol5reu9t1u2, ((uint8_t *)t42), t46);
                                                        }
                                                        goto gm_lb8;
                                                    }
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 286, 17);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t47 = gm_l8;
                                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                                        uint64_t t50 = r48;
                                        gm_l8 = t50;
                                    }
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t52 = (uint8_t)(t51 == ((uint8_t)1ULL));
                                if (!t52) {
                                    goto gm_lb16;
                                }
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)&F.l5) + 8), 64);
                                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 8));
                                gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                                gm_b16_8 t55 = gm_l1;
                                void * t56 = gm_l2;
                                gm_f_find_field_l0p9325(t54, t55, t56);
                                {
                                    {
                                        uint8_t t57 = (uint8_t)(((((uint8_t *)&F.l10) + 24))[16] != 0);
                                        if (!t57) {
                                            goto gm_lb18;
                                        }
                                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 24));
                                        gm_l11 = t58;
                                        gm_b16_8 t59 = gm_l11;
                                        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                                        gm_b16_8 t61 = gm_l1;
                                        void * t62 = gm_l2;
                                        gm_f_find_field_l0p9325(t60, t61, t62);
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                }
gm_lb17:;
                                goto gm_lb6;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 284, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 283, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t63 = gm_l4;
            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
            uint64_t t66 = r64;
            gm_l4 = t66;
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
__attribute__((aligned(4))) void gm_f_find_payload_field_i16vrf1(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_payload_field_i16vrf1 F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* variant */
    gm_b16_8 gm_l2; /* field */
    void * gm_l3; /* found */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l8; /* p */
    gm_b16_8 gm_l9; /* elems */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    gm_b16_8 gm_l15; /* e */
    GM_STACK_CHECK(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l16;
    gm_b16_8 t26 = gm_l0;
    gm_l4 = t26;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l5;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t32 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t30) + (uintptr_t)t32 * 72), 72);
                        {
                            {
                                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                if (!t34) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 64);
                                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                gm_b16_8 t36 = gm_l1;
                                uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
                                if (t37) {
                                    {
                                        {
                                            uint8_t t38 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l7) + 32)) != 0);
                                            if (!t38) {
                                                goto gm_lb9;
                                            }
                                            void * t39 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
                                            gm_l8 = t39;
                                            {
                                                {
                                                    void * t40 = gm_l8;
                                                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                                                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)10ULL));
                                                    if (!t42) {
                                                        goto gm_lb11;
                                                    }
                                                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((((uint8_t *)t40) + 8) + 8));
                                                    gm_l9 = t43;
                                                    gm_b16_8 t44 = gm_l9;
                                                    gm_l10 = t44;
                                                    gm_l11 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t45 = gm_l11;
                                                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                            uint8_t t47 = (t45 < t46);
                                                            if (!t47) {
                                                                goto gm_lb12;
                                                            }
                                                            {
                                                                void * t48 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                                uint64_t t50 = gm_l11;
                                                                {
                                                                    {
                                                                        memmove(((uint8_t *)&F.l12), (((uint8_t *)t48) + (uintptr_t)t50 * 136), 136);
                                                                        {
                                                                            {
                                                                                uint8_t t51 = (uint8_t)((((uint8_t *)&F.l12))[32] != 0);
                                                                                if (!t51) {
                                                                                    goto gm_lb18;
                                                                                }
                                                                                memmove(((uint8_t *)&gm_l13), ((uint8_t *)&F.l12), 32);
                                                                                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                                                gm_b16_8 t53 = gm_l2;
                                                                                uint8_t t54 = (gm_eq0iqiubfm7e(((uint8_t *)&t52), ((uint8_t *)&t53)));
                                                                                if (t54) {
                                                                                    void * t55 = gm_l3;
                                                                                    {
                                                                                        {
                                                                                            uint8_t t56 = (uint8_t)((((uint8_t *)t55))[12] != 0);
                                                                                            if (t56) {
                                                                                                goto gm_lb20;
                                                                                            }
                                                                                            gm_l16 = ((uint8_t)1);
                                                                                            goto gm_lb19;
                                                                                        }
gm_lb20:;
                                                                                        {
                                                                                            gm_l16 = ((uint8_t)0);
                                                                                            goto gm_lb19;
                                                                                        }
gm_lb21:;
                                                                                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 327, 44);
                                                                                    }
gm_lb19:;
                                                                                    uint8_t t57 = gm_l16;
                                                                                    if (t57) {
                                                                                        void * t58 = gm_l3;
                                                                                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                                                                                        gm_toceffb697p0u t60;
                                                                                        GM_ST(uint8_t, (uint8_t *)&t60 + 13, 0);
                                                                                        GM_ST(uint16_t, (uint8_t *)&t60 + 14, 0);
                                                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t60), t59);
                                                                                        ((uint8_t *)&t60)[12] = 1;
                                                                                        GM_ST(gm_toceffb697p0u, ((uint8_t *)t58), t60);
                                                                                    }
                                                                                    goto gm_lb12;
                                                                                }
                                                                                goto gm_lb17;
                                                                            }
gm_lb18:;
                                                                        }
gm_lb17:;
                                                                        goto gm_lb15;
                                                                    }
gm_lb16:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 324, 29);
                                                                }
gm_lb15:;
                                                            }
gm_lb14:;
                                                            uint64_t t61 = gm_l11;
                                                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                                            uint64_t t64 = r62;
                                                            gm_l11 = t64;
                                                        }
                                                    }
gm_lb12:;
                                                    goto gm_lb10;
                                                }
gm_lb11:;
                                            }
gm_lb10:;
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)1ULL));
                                if (!t66) {
                                    goto gm_lb22;
                                }
                                memmove(((uint8_t *)&F.l14), (((uint8_t *)&F.l6) + 8), 64);
                                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 8));
                                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                                gm_b16_8 t69 = gm_l1;
                                gm_b16_8 t70 = gm_l2;
                                void * t71 = gm_l3;
                                gm_f_find_payload_field_i16vrf1(t68, t69, t70, t71);
                                {
                                    {
                                        uint8_t t72 = (uint8_t)(((((uint8_t *)&F.l14) + 24))[16] != 0);
                                        if (!t72) {
                                            goto gm_lb24;
                                        }
                                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 24));
                                        gm_l15 = t73;
                                        gm_b16_8 t74 = gm_l15;
                                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                                        gm_b16_8 t76 = gm_l1;
                                        gm_b16_8 t77 = gm_l2;
                                        void * t78 = gm_l3;
                                        gm_f_find_payload_field_i16vrf1(t75, t76, t77, t78);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                }
gm_lb23:;
                                goto gm_lb6;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 319, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 318, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t79 = gm_l5;
            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
            uint64_t t82 = r80;
            gm_l5 = t82;
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
inline __attribute__((aligned(4))) void gm_f_find_variant_kifk1mm(gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_variant_kifk1mm F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* name */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l8; /* e */
    GM_STACK_CHECK(&F, 422u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l9;
    gm_b16_8 t15 = gm_l0;
    gm_l3 = t15;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l4;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t21 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t19) + (uintptr_t)t21 * 72), 72);
                        {
                            {
                                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                                if (!t23) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l5) + 8), 64);
                                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                gm_b16_8 t25 = gm_l1;
                                uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                                if (t26) {
                                    void * t27 = gm_l2;
                                    {
                                        {
                                            uint8_t t28 = (uint8_t)((((uint8_t *)t27))[28] != 0);
                                            if (t28) {
                                                goto gm_lb9;
                                            }
                                            gm_l9 = ((uint8_t)1);
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                        {
                                            gm_l9 = ((uint8_t)0);
                                            goto gm_lb8;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 306, 24);
                                    }
gm_lb8:;
                                    uint8_t t29 = gm_l9;
                                    if (t29) {
                                        void * t30 = gm_l2;
                                        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 16));
                                        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 48));
                                        gm_tt7dbdq4n1egc t33;
                                        GM_ST(uint8_t, (uint8_t *)&t33 + 25, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t33 + 26, 0);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t33 + 0), t31);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t33 + 12), t32);
                                        GM_ST(uint8_t, ((uint8_t *)&t33 + 24), ((uint8_t)1));
                                        gm_tkol5reu9t1u2 t34;
                                        GM_ST(uint8_t, (uint8_t *)&t34 + 29, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t34 + 30, 0);
                                        GM_ST(gm_tt7dbdq4n1egc, ((uint8_t *)&t34), t33);
                                        ((uint8_t *)&t34)[28] = 1;
                                        GM_ST(gm_tkol5reu9t1u2, ((uint8_t *)t30), t34);
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                                if (!t36) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l5) + 8), 64);
                                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
                                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                                gm_b16_8 t39 = gm_l1;
                                void * t40 = gm_l2;
                                gm_f_find_variant_kifk1mm(t38, t39, t40);
                                {
                                    {
                                        uint8_t t41 = (uint8_t)(((((uint8_t *)&F.l7) + 24))[16] != 0);
                                        if (!t41) {
                                            goto gm_lb13;
                                        }
                                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
                                        gm_l8 = t42;
                                        gm_b16_8 t43 = gm_l8;
                                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                                        gm_b16_8 t45 = gm_l1;
                                        void * t46 = gm_l2;
                                        gm_f_find_variant_kifk1mm(t44, t45, t46);
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                goto gm_lb6;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 303, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 302, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l4;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l4 = t50;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_use_l994th9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2) {
    struct gm_fr_fn_use_l994th9 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    uint32_t gm_l2; /* f */
    gm_tl874t2atnlh3 gm_l3; /* to */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l5;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 344));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 344) + 8));
    uint32_t t8 = gm_l2;
    uint64_t t9 = (uint64_t)t8;
    if ((uintptr_t)t9 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t9, t7, gm_srcb2hdor4f6g0q, 591, 11);
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t6) + (uintptr_t)t9 * 136) + 16));
    gm_l3 = t10;
    {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 224);
            uint32_t t13 = gm_l2;
            gm_tf5srh4mp4b3i t14 = gm_f_get_1ji4too(t12, t13);
            gm_l5 = t14;
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
            if (!t15) {
                goto gm_lb2;
            }
            uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
            gm_l4 = t16;
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l1;
            uint64_t t19 = gm_l4;
            gm_tsncjtib3j1l1 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t20 + 8), t19);
            gm_b0_1 t21 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t17, t18, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t22 = gm_l0;
        gm_tl874t2atnlh3 t23 = gm_l1;
        gm_tl874t2atnlh3 t24 = gm_l3;
        gm_b0_1 t25 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_expr_f16jm2b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_expr_f16jm2b F;
    void * gm_l0; /* w */
    void * gm_l1; /* f */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    void * t8 = gm_l1;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 56));
    gm_b0_1 t10 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t7, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = gm_l0;
    void * t12 = gm_l1;
    uint32_t t13 = GM_LD(uint32_t, (((((uint8_t *)t12) + 64) + 24) + 8));
    gm_b0_1 t14 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t11, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t15 = gm_l1;
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t15) + 40) + 8));
    gm_l2 = t16;
    {
        for (;;) {
            uint64_t t17 = gm_l3;
            uint64_t t18 = gm_l2;
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t20 = gm_l3;
                        gm_l4 = t20;
                        void * t21 = gm_l0;
                        void * t22 = gm_l1;
                        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 40));
                        uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 40) + 8));
                        uint64_t t25 = gm_l4;
                        if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_srcb2hdor4f6g0q, 716, 41);
                        void * t26 = (void *)(((uint8_t *)t23) + (uintptr_t)t25 * 64);
                        gm_tkn42u7na3t81 t27;
                        memset(&t27, 0, sizeof t27);
                        void * t28 = gm_l1;
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 40));
                        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 40) + 8));
                        uint64_t t31 = gm_l4;
                        if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srcb2hdor4f6g0q, 716, 58);
                        uint32_t t32 = GM_LD(uint32_t, (((((uint8_t *)t29) + (uintptr_t)t31 * 64) + 48) + 8));
                        gm_b0_1 t33 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t21, t26, t27, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 716, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l3;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l3 = t37;
        }
    }
gm_lb1:;
    {
        void * t38 = gm_l0;
        void * t39 = gm_l1;
        gm_b0_1 t40 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t38, &(*(gm_t1s75i08qtptd *)((uint8_t *)t39)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t41 = gm_l0;
    void * t42 = gm_l1;
    void * t43 = (void *)(((uint8_t *)t42) + 64);
    gm_b0_1 t44 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t41, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        void * t45 = gm_l0;
        void * t46 = gm_l1;
        gm_f_pop_label_e8oj3h3(t45, &(*(gm_t1s75i08qtptd *)((uint8_t *)t46)));
    }
    void * t47 = gm_l0;
    gm_f_pop_scope_vgka5ae(t47);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_6oqub48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_if_expr_6oqub48 F;
    void * gm_l0; /* w */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* br */
    gm_t08olq97rscn3 gm_l8; /* name */
    void * gm_l9; /* body */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t12 = gm_l1;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    gm_l2 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l3;
            uint64_t t15 = gm_l2;
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t17 = gm_l3;
                        gm_l4 = t17;
                        void * t18 = gm_l1;
                        void * t19 = GM_LD(void *, ((uint8_t *)t18));
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
                        uint64_t t21 = gm_l4;
                        if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srcb2hdor4f6g0q, 681, 16);
                        void * t22 = (void *)(((uint8_t *)t19) + (uintptr_t)t21 * 72);
                        gm_l5 = t22;
                        void * t23 = gm_l0;
                        void * t24 = gm_l5;
                        uint32_t t25 = GM_LD(uint32_t, (((((uint8_t *)t24) + 32) + 24) + 8));
                        gm_b0_1 t26 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t23, t25);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t27 = gm_l0;
                        void * t28 = gm_l5;
                        void * t29 = (void *)((uint8_t *)t28);
                        gm_b0_1 t30 = gm_f_cond_d94lif5((void *)NULL, cx, NULL, t27, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t31 = gm_l0;
                        void * t32 = gm_l5;
                        void * t33 = (void *)(((uint8_t *)t32) + 32);
                        gm_b0_1 t34 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t31, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t35 = gm_l0;
                        gm_f_pop_scope_vgka5ae(t35);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 680, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t36 = gm_l3;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l3 = t39;
        }
    }
gm_lb1:;
    {
        {
            void * t40 = gm_l1;
            uint8_t t41 = (uint8_t)(((((uint8_t *)t40) + 16))[48] != 0);
            if (!t41) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)t40) + 16), 48);
            {
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                    if (!t43) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 40);
                    void * t44 = gm_l0;
                    void * t45 = (void *)((uint8_t *)&F.l7);
                    gm_b0_1 t46 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t44, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                    if (!t48) {
                        goto gm_lb10;
                    }
                    {
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)&F.l6) + 8), 32);
                    }
                    void * t49 = GM_LD(void *, ((((uint8_t *)&F.l6) + 8) + 32));
                    gm_l9 = t49;
                    void * t50 = gm_l0;
                    void * t51 = gm_l9;
                    uint32_t t52 = GM_LD(uint32_t, ((((uint8_t *)t51) + 72) + 8));
                    gm_b0_1 t53 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t50, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t54 = gm_l0;
                        uint32_t t55 = GM_LD(uint32_t, ((((uint8_t *)&gm_l8) + 16) + 8));
                        gm_t9qkh0rsooc8d t56;
                        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)8ULL));
                        gm_b0_1 t57 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t54, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t56, t55);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t58 = gm_l0;
                    void * t59 = gm_l9;
                    gm_b0_1 t60 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t58, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t61 = gm_l0;
                    gm_f_pop_scope_vgka5ae(t61);
                    goto gm_lb8;
                }
gm_lb10:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 688, 9);
            }
gm_lb8:;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_use_9lcr9t0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_label_use_9lcr9t0 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t5 = (uint8_t)((((uint8_t *)a1))[32] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l2), ((uint8_t *)a1), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 72) + 8));
    gm_l3 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 > ((uint64_t)0ULL));
            if (!t9) {
                goto gm_lb1;
            }
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 740, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, (((uint8_t *)t14) + 72));
                uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 72) + 8));
                uint64_t t17 = gm_l3;
                if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srcb2hdor4f6g0q, 741, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)t17 * 32));
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (t20) {
                    void * t21 = gm_l0;
                    gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                    void * t23 = gm_l0;
                    void * t24 = GM_LD(void *, (((uint8_t *)t23) + 72));
                    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 72) + 8));
                    uint64_t t26 = gm_l3;
                    if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcb2hdor4f6g0q, 742, 31);
                    gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t24) + (uintptr_t)t26 * 32) + 16));
                    gm_b0_1 t28 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t21, t22, t27);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_local_binding_25t1g13(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_local_binding_25t1g13 F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    void * gm_l2; /* t */
    void * gm_l3; /* lit */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* head */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 592u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l9;
    {
        {
            void * t11 = gm_l1;
            uint8_t t12 = (uint8_t)(GM_LD(void *, (((uint8_t *)t11) + 24)) != 0);
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = GM_LD(void *, (((uint8_t *)t11) + 24));
            gm_l2 = t13;
            void * t14 = gm_l0;
            void * t15 = gm_l2;
            gm_b0_1 t16 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t14, t15);
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
            void * t17 = gm_l1;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 40));
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)36ULL));
            if (!t20) {
                goto gm_lb4;
            }
            void * t21 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 8));
            gm_l3 = t21;
            void * t22 = gm_l1;
            uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t22) + 8) + 8));
            uint8_t t24 = ((t23 == ((uint64_t)1ULL)));
            if (t24) {
                {
                    void * t25 = gm_l1;
                    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 8));
                    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 8) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t27, gm_srcb2hdor4f6g0q, 652, 21);
                    memmove(((uint8_t *)&gm_l4), (((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 32), 32);
                }
                {
                    void * t28 = gm_l0;
                    uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 16) + 8));
                    gm_t9qkh0rsooc8d t30;
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
                    gm_b0_1 t31 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t28, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)), t30, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 16));
                uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 16) + 4));
                void * t34 = gm_l3;
                uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)t34) + 56) + 8));
                gm_tl874t2atnlh3 t36;
                GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t32);
                GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t33);
                GM_ST(uint32_t, ((uint8_t *)&t36 + 8), t35);
                gm_l5 = t36;
                {
                    void * t37 = gm_l0;
                    void * t38 = GM_LD(void *, (((uint8_t *)t37) + 16));
                    void * t39 = (void *)(((uint8_t *)t38) + 40);
                    gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                    void * t41 = gm_l0;
                    void * t42 = GM_LD(void *, ((uint8_t *)t41));
                    gm_tl874t2atnlh3 t43 = gm_l5;
                    gm_b16_8 t44 = gm_f_src_fjcc76p(t42, GM_CONV(gm_cseahvg8v7p0vs, t43));
                    gm_b16_8 t45 = gm_f_trim_fdrfe6i(t44);
                    gm_th2lqbo77pvsc t46;
                    memset(&t46, 0, sizeof t46);
                    gm_t6ok5tvcjc200 t47;
                    GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t45);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t47 + 16), t46);
                    gm_tftb1odc739t5 t48 = gm_f_put_rt5d34n(t39, GM_CONV(gm_cseahvg8v7p0vs, t40), &t47);
                    gm_l9 = t48;
                }
                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcb2hdor4f6g0q, 656, 13);
                void * t50 = gm_l0;
                void * t51 = gm_l1;
                void * t52 = GM_LD(void *, (((uint8_t *)t51) + 40));
                gm_b0_1 t53 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t50, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t54 = gm_l0;
    void * t55 = gm_l1;
    void * t56 = GM_LD(void *, (((uint8_t *)t55) + 40));
    gm_b0_1 t57 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t54, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t58 = gm_l1;
    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t58) + 8));
    gm_l6 = t59;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t60 = gm_l7;
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t62 = (t60 < t61);
            if (!t62) {
                goto gm_lb5;
            }
            {
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t65 = gm_l7;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l8), (((uint8_t *)t63) + (uintptr_t)t65 * 32), 32);
                        }
                        {
                            void * t66 = gm_l0;
                            void * t67 = gm_l1;
                            uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)t67) + 48) + 8));
                            gm_t9qkh0rsooc8d t69;
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)8ULL));
                            gm_b0_1 t70 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t66, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t69, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 662, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t71 = gm_l7;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            uint64_t t74 = r72;
            gm_l7 = t74;
        }
    }
gm_lb5:;
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
inline __attribute__((aligned(4))) void gm_f_lookup_decl_10pnl6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_lookup_decl_10pnl6c F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* d */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
            gm_b16_8 t8 = gm_l1;
            gm_tf5srh4mp4b3i t9 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t5, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t9;
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_l2 = t11;
            {
                uint64_t t12 = gm_l2;
                gm_tsncjtib3j1l1 t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t13 + 8), t12);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 28, 0);
                GM_ST(gm_tsncjtib3j1l1, ((uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out)), t13);
                ((uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t14 = gm_l1;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s4g2k7ups3ui6));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
    uint8_t t16 = (gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
    if (t16) {
        void * t17 = gm_l0;
        memmove(gm_out, (((uint8_t *)t17) + 280), 32);
        goto gm_fdone;
    }
    memset(&(*(gm_t1335qftqvd8f *)gm_out), 0, sizeof (*(gm_t1335qftqvd8f *)gm_out));
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_lookup_local_tdg7skl(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_tdg7skl F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* names */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 32) + 8));
    gm_l2 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) {
                goto gm_lb1;
            }
            {
                uint64_t t12 = gm_l2;
                uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
                if (o14) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 136, 9);
                uint64_t t15 = r13;
                gm_l2 = t15;
                void * t16 = gm_l0;
                void * t17 = GM_LD(void *, (((uint8_t *)t16) + 32));
                uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 32) + 8));
                uint64_t t19 = gm_l2;
                if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcb2hdor4f6g0q, 137, 19);
                void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 48);
                gm_l3 = t20;
                void * t21 = gm_l3;
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
                gm_l4 = t22;
                {
                    for (;;) {
                        uint64_t t23 = gm_l4;
                        uint8_t t24 = (t23 > ((uint64_t)0ULL));
                        if (!t24) {
                            goto gm_lb4;
                        }
                        {
                            uint64_t t25 = gm_l4;
                            uint64_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint64_t)1ULL), &r26);
                            if (o27) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 140, 13);
                            uint64_t t28 = r26;
                            gm_l4 = t28;
                            void * t29 = gm_l3;
                            void * t30 = GM_LD(void *, ((uint8_t *)t29));
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t29) + 8));
                            uint64_t t32 = gm_l4;
                            if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_srcb2hdor4f6g0q, 141, 16);
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t30) + (uintptr_t)t32 * 32));
                            gm_b16_8 t34 = gm_l1;
                            uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                            if (t35) {
                                void * t36 = gm_l3;
                                void * t37 = GM_LD(void *, ((uint8_t *)t36));
                                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
                                uint64_t t39 = gm_l4;
                                if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcb2hdor4f6g0q, 141, 53);
                                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t37) + (uintptr_t)t39 * 32) + 16));
                                gm_toceffb697p0u t41;
                                GM_ST(uint8_t, (uint8_t *)&t41 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t41 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t41), t40);
                                ((uint8_t *)&t41)[12] = 1;
                                F.ret = t41;
                                goto gm_fret;
                            }
                        }
gm_lb6:;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_toceffb697p0u t42;
    memset(&t42, 0, sizeof t42);
    F.ret = t42;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_match_expr_j2bvhsh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_match_expr_j2bvhsh F;
    void * gm_l0; /* w */
    void * gm_l1; /* m */
    gm_tkn42u7na3t81 gm_l2; /* ty */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* arm */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    void * gm_l10; /* g */
    GM_STACK_CHECK(&F, 487u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t14 = gm_l0;
    void * t15 = gm_l1;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    gm_b0_1 t17 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t14, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t18 = gm_l0;
    void * t19 = gm_l1;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
    gm_tkn42u7na3t81 t22 = gm_f_type_of_tiqb21n(t18, GM_CONV(gm_cseahvg8v7p0vs, t21));
    gm_l2 = t22;
    gm_l4 = ((uint64_t)0ULL);
    void * t23 = gm_l1;
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 8) + 8));
    gm_l3 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l4;
            uint64_t t26 = gm_l3;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l4;
                        gm_l5 = t28;
                        void * t29 = gm_l1;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 8));
                        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 8) + 8));
                        uint64_t t32 = gm_l5;
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_srcb2hdor4f6g0q, 704, 17);
                        void * t33 = (void *)(((uint8_t *)t30) + (uintptr_t)t32 * 48);
                        gm_l6 = t33;
                        void * t34 = gm_l0;
                        void * t35 = gm_l6;
                        uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)t35) + 32) + 8));
                        gm_b0_1 t37 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t34, t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = ((uint64_t)0ULL);
                        void * t38 = gm_l6;
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 8));
                        gm_l7 = t39;
                        {
                            for (;;) {
                                uint64_t t40 = gm_l8;
                                uint64_t t41 = gm_l7;
                                uint8_t t42 = (t40 < t41);
                                if (!t42) {
                                    goto gm_lb6;
                                }
                                {
                                    {
                                        {
                                            uint64_t t43 = gm_l8;
                                            gm_l9 = t43;
                                            void * t44 = gm_l0;
                                            void * t45 = gm_l6;
                                            void * t46 = GM_LD(void *, ((uint8_t *)t45));
                                            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t45) + 8));
                                            uint64_t t48 = gm_l9;
                                            if ((uintptr_t)t48 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t48, t47, gm_srcb2hdor4f6g0q, 706, 47);
                                            void * t49 = (void *)(((uint8_t *)t46) + (uintptr_t)t48 * 64);
                                            gm_tkn42u7na3t81 t50 = gm_l2;
                                            void * t51 = gm_l6;
                                            void * t52 = GM_LD(void *, ((uint8_t *)t51));
                                            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t51) + 8));
                                            uint64_t t54 = gm_l9;
                                            if ((uintptr_t)t54 >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)t54, t53, gm_srcb2hdor4f6g0q, 706, 64);
                                            uint32_t t55 = GM_LD(uint32_t, (((((uint8_t *)t52) + (uintptr_t)t54 * 64) + 48) + 8));
                                            gm_b0_1 t56 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t44, t49, t50, t55);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 706, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t57 = gm_l8;
                                uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                                uint64_t t60 = r58;
                                gm_l8 = t60;
                            }
                        }
gm_lb6:;
                        {
                            {
                                void * t61 = gm_l6;
                                uint8_t t62 = (uint8_t)(GM_LD(void *, (((uint8_t *)t61) + 16)) != 0);
                                if (!t62) {
                                    goto gm_lb12;
                                }
                                void * t63 = GM_LD(void *, (((uint8_t *)t61) + 16));
                                gm_l10 = t63;
                                void * t64 = gm_l0;
                                void * t65 = gm_l10;
                                gm_b0_1 t66 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t64, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        void * t67 = gm_l0;
                        void * t68 = gm_l6;
                        void * t69 = GM_LD(void *, (((uint8_t *)t68) + 24));
                        gm_b0_1 t70 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t67, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t71 = gm_l0;
                        gm_f_pop_scope_vgka5ae(t71);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 703, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t72 = gm_l4;
            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
            uint64_t t75 = r73;
            gm_l4 = t75;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_member_def_9f4kiah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_member_def_9f4kiah F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* owner */
    gm_tkol5reu9t1u2 gm_l6; /* found */
    void * gm_l7; /* lit */
    void * gm_l8; /* lit */
    void * gm_l9; /* inner */
    void * gm_l10; /* lit */
    void * gm_l11; /* lit */
    gm_tt7dbdq4n1egc gm_l12; /* f */
    gm_b16_8 gm_l13; /* text */
    gm_b16_8 gm_l14; /* code */
    GM_STACK_CHECK(&F, 1023u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l15;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 240));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 240) + 8));
    uint64_t t18 = gm_l1;
    if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcb2hdor4f6g0q, 346, 14);
    void * t19 = (void *)(((uint8_t *)t16) + (uintptr_t)t18 * 216);
    gm_l3 = t19;
    void * t20 = gm_l3;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)t20));
    gm_l4 = t21;
    {
        {
            {
                void * t22 = gm_l3;
                uint8_t t23 = GM_LD(uint8_t, (((((uint8_t *)t22) + 40) + 64) + 0));
                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                if (!t24) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l5), (((((uint8_t *)t22) + 40) + 64) + 8), 64);
            }
            {
                gm_tkol5reu9t1u2 t25;
                memset(&t25, 0, sizeof t25);
                gm_l6 = t25;
            }
            void * t26 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)32ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    void * t29 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l7 = t29;
                    void * t30 = gm_l7;
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 40));
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    gm_b16_8 t33 = gm_l2;
                    void * t34 = (void *)((uint8_t *)&gm_l6);
                    gm_f_find_field_l0p9325(t32, t33, t34);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)33ULL));
                    if (!t36) {
                        goto gm_lb5;
                    }
                    void * t37 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l8 = t37;
                    void * t38 = gm_l8;
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 48));
                    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                    gm_b16_8 t41 = gm_l2;
                    void * t42 = (void *)((uint8_t *)&gm_l6);
                    gm_f_find_variant_kifk1mm(t40, t41, t42);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)35ULL));
                    if (!t44) {
                        goto gm_lb6;
                    }
                    void * t45 = GM_LD(void *, (((((uint8_t *)t26) + 8) + 8) + 16));
                    gm_l9 = t45;
                    void * t46 = gm_l9;
                    {
                        {
                            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)32ULL));
                            if (!t48) {
                                goto gm_lb8;
                            }
                            void * t49 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
                            gm_l10 = t49;
                            void * t50 = gm_l10;
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 40));
                            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                            gm_b16_8 t53 = gm_l2;
                            void * t54 = (void *)((uint8_t *)&gm_l6);
                            gm_f_find_field_l0p9325(t52, t53, t54);
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)33ULL));
                            if (!t56) {
                                goto gm_lb9;
                            }
                            void * t57 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
                            gm_l11 = t57;
                            void * t58 = gm_l11;
                            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t58) + 48));
                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                            gm_b16_8 t61 = gm_l2;
                            void * t62 = (void *)((uint8_t *)&gm_l6);
                            gm_f_find_variant_kifk1mm(t60, t61, t62);
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            goto gm_lb7;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 354, 13);
                    }
gm_lb7:;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    goto gm_lb3;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 350, 5);
            }
gm_lb3:;
            uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l6))[28] != 0);
            if (t63) {
                memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l6), 28);
            } else {
                gm_toceffb697p0u t64;
                memset(&t64, 0, sizeof t64);
                F.ret = t64;
                goto gm_fret;
            }
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, ((uint8_t *)t65));
            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 12));
            gm_b16_8 t68 = gm_f_src_fjcc76p(t66, GM_CONV(gm_cseahvg8v7p0vs, t67));
            gm_b16_8 t69 = gm_f_trim_fdrfe6i(t68);
            gm_l13 = t69;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 24));
            if (t70) {
                {
                    gm_b16_8 t71 = gm_l4;
                    gm_b16_8 t72 = gm_l13;
                    gm_t6j6smj7qoctu t73;
                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t71);
                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 16), t72);
                    gm_f_format_ahgk48j((void *)&gm_l15, (void *)NULL, cx, NULL, &t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcb2hdor4f6g0q, 364, 31);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                gm_l14 = t76;
            } else {
                gm_b16_8 t77 = gm_l13;
                gm_l14 = t77;
            }
            {
                void * t78 = gm_l0;
                gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
                gm_b16_8 t80 = gm_l14;
                gm_th2lqbo77pvsc t81;
                memset(&t81, 0, sizeof t81);
                gm_t6ok5tvcjc200 t82;
                GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t80);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t82 + 16), t81);
                gm_b0_1 t83 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t78, t79, &t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
            gm_toceffb697p0u t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t85), t84);
            ((uint8_t *)&t85)[12] = 1;
            F.ret = t85;
            goto gm_fret;
        }
gm_lb2:;
        gm_toceffb697p0u t86;
        memset(&t86, 0, sizeof t86);
        F.ret = t86;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_name_use_73pph7t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_name_use_73pph7t F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* def */
    gm_tsncjtib3j1l1 gm_l3; /* d */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toceffb697p0u gm_l4;
    gm_t1335qftqvd8f gm_l5;
    {
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_toceffb697p0u t8 = gm_f_lookup_local_tdg7skl(t6, t7);
            gm_l4 = t8;
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l4))[12] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l4));
            gm_l2 = t10;
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_tl874t2atnlh3 t13 = gm_l2;
            gm_b0_1 t14 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    void * t15 = gm_l0;
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)a1));
                    gm_f_lookup_decl_10pnl6c((void *)&gm_l5, (void *)NULL, cx, NULL, t15, t16);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l5))[24] != 0);
                if (!t17) {
                    goto gm_lb4;
                }
                {
                    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l5), 24);
                }
                {
                    void * t18 = gm_l0;
                    gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
                    gm_b0_1 t20 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t18, t19, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l3)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto gm_lb3;
            }
gm_lb4:;
        }
gm_lb3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_note_receiver_6mb4kc5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_note_receiver_6mb4kc5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tsncjtib3j1l1 gm_l2; /* d */
    uint64_t gm_l3; /* key */
    gm_b16_8 gm_l4; /* r */
    uint64_t gm_l5; /* x */
    uint64_t gm_l7; /* m */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 745u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_th2lqbo77pvsc gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tlabdfckddbfd gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    uint64_t t11 = gm_f_ref_key_vam5ppg(&(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
    gm_l3 = t11;
    {
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 168);
            uint64_t t14 = gm_l3;
            gm_f_get_o6c8ntf((void *)&gm_l10, t13, t14);
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l4 = t16;
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 16));
            void * t19 = (void *)(((uint8_t *)t18) + 272);
            gm_tl874t2atnlh3 t20 = gm_l1;
            gm_b16_8 t21 = gm_l4;
            gm_tftb1odc739t5 t22 = gm_f_put_ss3ma85(t19, GM_CONV(gm_cseahvg8v7p0vs, t20), t21);
            gm_l11 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcb2hdor4f6g0q, 215, 9);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)0ULL));
            if (!t25) {
                goto gm_lb4;
            }
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_l5 = t26;
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            uint64_t t29 = gm_l5;
            gm_f_decl_done_mk70ol9((void *)&gm_l12, (void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l12))[208] != 0);
            if (t30) {
                memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l12), 208);
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)9ULL));
                    if (!t32) {
                        goto gm_lb6;
                    }
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    gm_l7 = t33;
                    void * t34 = gm_l0;
                    void * t35 = GM_LD(void *, ((uint8_t *)t34));
                    uint64_t t36 = gm_l7;
                    void * t37 = gm_l0;
                    void * t38 = GM_LD(void *, ((uint8_t *)t37));
                    void * t39 = GM_LD(void *, ((uint8_t *)t38));
                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 240));
                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 240) + 8));
                    uint64_t t42 = gm_l5;
                    if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcb2hdor4f6g0q, 221, 46);
                    uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t40) + (uintptr_t)t42 * 216) + 32));
                    gm_f_module_members_26b4s91((void *)&F.l9, (void *)NULL, cx, NULL, t35, t36, t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)0ULL));
                    if (!t45) {
                        goto gm_lb7;
                    }
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                    gm_l8 = t46;
                    void * t47 = gm_l0;
                    void * t48 = GM_LD(void *, ((uint8_t *)t47));
                    gm_t88rcucg1b7uk t49 = gm_l8;
                    gm_f_variant_members_nesi2hv((void *)&F.l9, (void *)NULL, cx, NULL, t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
                    if (!t51) {
                        goto gm_lb8;
                    }
                    void * t52 = gm_l0;
                    void * t53 = GM_LD(void *, ((uint8_t *)t52));
                    uint64_t t54 = gm_l5;
                    gm_f_operation_members_62l824c((void *)&F.l9, (void *)NULL, cx, NULL, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb5;
                }
gm_lb8:;
                {
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 220, 25);
            }
gm_lb5:;
            void * t55 = gm_l0;
            void * t56 = (void *)(((uint8_t *)t55) + 168);
            uint64_t t57 = gm_l3;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_tftb1odc739t5 t60 = gm_f_put_hku1qp6(t56, t57, t59);
            gm_l13 = t60;
            uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcb2hdor4f6g0q, 226, 5);
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 16));
            void * t64 = (void *)(((uint8_t *)t63) + 272);
            gm_tl874t2atnlh3 t65 = gm_l1;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_tftb1odc739t5 t68 = gm_f_put_ss3ma85(t64, GM_CONV(gm_cseahvg8v7p0vs, t65), t67);
            gm_l14 = t68;
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcb2hdor4f6g0q, 227, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_operation_use_ue1or58(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_operation_use_ue1or58 F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    void * gm_l7;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = gm_l1;
    void * t10 = gm_f_effect_lit_26tkk47(t8, t9);
    gm_l7 = t10;
    uint8_t t11 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l7)) != 0);
    if (t11) {
        void * t12 = GM_LD(void *, ((uint8_t *)&gm_l7));
        gm_l3 = t12;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_l5 = ((uint64_t)0ULL);
    void * t13 = gm_l3;
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 24) + 8));
    gm_l4 = t14;
    {
        for (;;) {
            uint64_t t15 = gm_l5;
            uint64_t t16 = gm_l4;
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t18 = gm_l5;
                        gm_l6 = t18;
                        void * t19 = gm_l3;
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 24));
                        uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 24) + 8));
                        uint64_t t22 = gm_l6;
                        if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcb2hdor4f6g0q, 526, 12);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t20) + (uintptr_t)t22 * 80));
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                        uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                        if (t25) {
                            void * t26 = gm_l0;
                            gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                            uint64_t t28 = gm_l1;
                            uint64_t t29 = gm_l6;
                            uint32_t t30 = (uint32_t)t29;
                            gm_tn68el8d14vku t31;
                            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t28);
                            GM_ST(uint32_t, ((uint8_t *)&t31 + 8), t30);
                            gm_tsncjtib3j1l1 t32;
                            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t32 + 8), t31);
                            gm_b0_1 t33 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t26, t27, &t32);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 525, 5);
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_path_type_flfaob1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_path_type_flfaob1 F;
    void * gm_l0; /* w */
    void * gm_l1; /* path */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_tsncjtib3j1l1 gm_l3; /* d */
    uint64_t gm_l4; /* x */
    gm_t88rcucg1b7uk gm_l6; /* t */
    void * gm_l7; /* base */
    uint64_t gm_l8; /* d */
    gm_t88rcucg1b7uk gm_l10; /* t */
    GM_STACK_CHECK(&F, 906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    gm_toceffb697p0u gm_l12;
    gm_t1335qftqvd8f gm_l13;
    gm_tlabdfckddbfd gm_l14;
    gm_tu0a56t4bfvqh gm_l15;
    gm_tlabdfckddbfd gm_l16;
    void * t17 = gm_l1;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)6ULL));
            if (!t19) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t17) + 8) + 8), 32);
            }
            void * t20 = gm_l0;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_toceffb697p0u t22 = gm_f_lookup_local_tdg7skl(t20, t21);
            gm_l12 = t22;
            {
                {
                    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l12))[12] != 0);
                    if (t23) {
                        goto gm_lb4;
                    }
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 803, 16);
            }
gm_lb3:;
            uint8_t t24 = gm_l11;
            if (t24) {
                gm_tkn42u7na3t81 t25;
                memset(&t25, 0, sizeof t25);
                F.ret = t25;
                goto gm_fret;
            }
            {
                void * t26 = gm_l0;
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_f_lookup_decl_10pnl6c((void *)&gm_l13, (void *)NULL, cx, NULL, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
            if (t28) {
                memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l13), 24);
            } else {
                gm_tkn42u7na3t81 t29;
                memset(&t29, 0, sizeof t29);
                F.ret = t29;
                goto gm_fret;
            }
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)0ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    gm_l4 = t32;
                    void * t33 = gm_l0;
                    void * t34 = GM_LD(void *, ((uint8_t *)t33));
                    uint64_t t35 = gm_l4;
                    gm_f_decl_done_mk70ol9((void *)&gm_l14, (void *)NULL, cx, NULL, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l14))[208] != 0);
                    if (t36) {
                        memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l14), 208);
                    } else {
                        gm_tkn42u7na3t81 t37;
                        memset(&t37, 0, sizeof t37);
                        F.ret = t37;
                        goto gm_fret;
                    }
                    {
                        {
                            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)0ULL));
                            if (!t39) {
                                goto gm_lb9;
                            }
                            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l6 = t40;
                            gm_t88rcucg1b7uk t41 = gm_l6;
                            gm_tkn42u7na3t81 t42;
                            GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
                            ((uint8_t *)&t42)[4] = 1;
                            F.ret = t42;
                            goto gm_fret;
                        }
gm_lb9:;
                    }
gm_lb8:;
                    gm_tkn42u7na3t81 t43;
                    memset(&t43, 0, sizeof t43);
                    F.ret = t43;
                    goto gm_fret;
                }
gm_lb7:;
                gm_tkn42u7na3t81 t44;
                memset(&t44, 0, sizeof t44);
                F.ret = t44;
                goto gm_fret;
            }
gm_lb6:;
        }
gm_lb2:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)22ULL));
            if (!t46) {
                goto gm_lb10;
            }
            void * t47 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l7 = t47;
            {
                {
                    void * t48 = gm_l0;
                    void * t49 = gm_l7;
                    gm_tu0a56t4bfvqh t50 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t50;
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)1ULL));
                    if (!t52) {
                        goto gm_lb12;
                    }
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    gm_l8 = t53;
                    void * t54 = gm_l0;
                    void * t55 = GM_LD(void *, ((uint8_t *)t54));
                    uint64_t t56 = gm_l8;
                    gm_f_decl_done_mk70ol9((void *)&gm_l16, (void *)NULL, cx, NULL, t55, t56);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l16))[208] != 0);
                    if (t57) {
                        memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l16), 208);
                    } else {
                        gm_tkn42u7na3t81 t58;
                        memset(&t58, 0, sizeof t58);
                        F.ret = t58;
                        goto gm_fret;
                    }
                    {
                        {
                            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                            if (!t60) {
                                goto gm_lb14;
                            }
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 8));
                            gm_l10 = t61;
                            gm_t88rcucg1b7uk t62 = gm_l10;
                            gm_tkn42u7na3t81 t63;
                            GM_ST(uint8_t, (uint8_t *)&t63 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t63 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t63), t62);
                            ((uint8_t *)&t63)[4] = 1;
                            F.ret = t63;
                            goto gm_fret;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    goto gm_lb11;
                }
gm_lb12:;
            }
gm_lb11:;
            gm_tkn42u7na3t81 t64;
            memset(&t64, 0, sizeof t64);
            F.ret = t64;
            goto gm_fret;
        }
gm_lb10:;
        {
            gm_tkn42u7na3t81 t65;
            memset(&t65, 0, sizeof t65);
            F.ret = t65;
            goto gm_fret;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 801, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_jo09k6f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, uint32_t a3) {
    struct gm_fr_pattern_jo09k6f F;
    void * gm_l0; /* w */
    void * gm_l1; /* p */
    gm_tkn42u7na3t81 gm_l2; /* ty */
    uint32_t gm_l3; /* from */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* e */
    void * gm_l7; /* lo */
    void * gm_l8; /* hi */
    void * gm_l9; /* inner */
    gm_tkn42u7na3t81 gm_l10; /* err */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_tagc77imt2chm gm_l13; /* pp */
    gm_tkn42u7na3t81 gm_l14; /* owner */
    gm_t08olq97rscn3 gm_l15; /* id */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_t88rcucg1b7uk gm_l17; /* pt */
    gm_b16_8 gm_l18; /* args */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* a */
    gm_tkn42u7na3t81 gm_l23; /* sub */
    gm_t08olq97rscn3 gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_t08olq97rscn3 gm_l26; /* v */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_b16_8 gm_l28; /* args */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    GM_STACK_CHECK(&F, 1116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l32;
    gm_tkn42u7na3t81 gm_l33;
    void * t42 = gm_l1;
    {
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
            if (!t44) {
                goto gm_lb2;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)4ULL));
            if (!t46) {
                goto gm_lb3;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
            if (!t48) {
                goto gm_lb4;
            }
            {
                memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t42) + 8) + 8), 32);
            }
            {
                void * t49 = gm_l0;
                uint32_t t50 = gm_l3;
                gm_t9qkh0rsooc8d t51;
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)8ULL));
                gm_b0_1 t52 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t49, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)), t51, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)2ULL));
            if (!t54) {
                goto gm_lb5;
            }
            {
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t42) + 8) + 8), 32);
            }
            {
                void * t55 = gm_l0;
                uint32_t t56 = gm_l3;
                gm_t9qkh0rsooc8d t57;
                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)8ULL));
                gm_b0_1 t58 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t55, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t57, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)3ULL));
            if (!t60) {
                goto gm_lb6;
            }
            void * t61 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
            gm_l6 = t61;
            void * t62 = gm_l0;
            void * t63 = gm_l6;
            gm_b0_1 t64 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)5ULL));
            if (!t66) {
                goto gm_lb7;
            }
            void * t67 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
            gm_l7 = t67;
            void * t68 = GM_LD(void *, (((((uint8_t *)t42) + 8) + 8) + 8));
            gm_l8 = t68;
            void * t69 = gm_l0;
            void * t70 = gm_l7;
            gm_b0_1 t71 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t72 = gm_l0;
            void * t73 = gm_l8;
            gm_b0_1 t74 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)6ULL));
            if (!t76) {
                goto gm_lb8;
            }
            void * t77 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
            gm_l9 = t77;
            gm_tkn42u7na3t81 t78;
            memset(&t78, 0, sizeof t78);
            gm_l10 = t78;
            {
                {
                    uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
                    if (!t79) {
                        goto gm_lb10;
                    }
                    gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                    gm_l11 = t80;
                    {
                        {
                            void * t81 = gm_l0;
                            void * t82 = GM_LD(void *, ((uint8_t *)t81));
                            void * t83 = (void *)(((uint8_t *)t82) + 8);
                            gm_t88rcucg1b7uk t84 = gm_l11;
                            gm_f_kind_jcnt4hm((void *)&gm_l32, t83, GM_CONV(gm_csguankko4lmhd, t84));
                            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)14ULL));
                            if (!t86) {
                                goto gm_lb12;
                            }
                            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l32) + 8));
                            gm_l12 = t87;
                            gm_t88rcucg1b7uk t88 = gm_l12;
                            gm_tkn42u7na3t81 t89;
                            GM_ST(uint8_t, (uint8_t *)&t89 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t89 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89), t88);
                            ((uint8_t *)&t89)[4] = 1;
                            gm_l10 = t89;
                            goto gm_lb11;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    goto gm_lb9;
                }
gm_lb10:;
            }
gm_lb9:;
            void * t90 = gm_l0;
            void * t91 = gm_l9;
            gm_tkn42u7na3t81 t92 = gm_l10;
            uint32_t t93 = gm_l3;
            gm_b0_1 t94 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t90, t91, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)7ULL));
            if (!t96) {
                goto gm_lb13;
            }
            memmove(((uint8_t *)&gm_l13), ((((uint8_t *)t42) + 8) + 8), 32);
            gm_tkn42u7na3t81 t97 = gm_l2;
            gm_l14 = t97;
            {
                {
                    void * t98 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t98) + 8) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)8ULL));
                    if (!t100) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&gm_l15), ((((uint8_t *)t98) + 8) + 8), 32);
                    {
                        {
                            uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
                            if (!t101) {
                                goto gm_lb17;
                            }
                            gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                            gm_l16 = t102;
                            void * t103 = gm_l0;
                            gm_t88rcucg1b7uk t104 = gm_l16;
                            gm_tf5srh4mp4b3i t105 = gm_f_variant_use_29ooua3((void *)NULL, cx, NULL, t103, t104, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l15)));
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
                void * t106 = gm_l0;
                void * t107 = GM_LD(void *, ((uint8_t *)&gm_l13));
                gm_b0_1 t108 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                {
                    {
                        void * t109 = gm_l0;
                        void * t110 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        gm_tkn42u7na3t81 t111 = gm_f_path_type_flfaob1((void *)NULL, cx, NULL, t109, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l33 = t111;
                        uint8_t t112 = (uint8_t)((((uint8_t *)&gm_l33))[4] != 0);
                        if (!t112) {
                            goto gm_lb19;
                        }
                        gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l33));
                        gm_l17 = t113;
                        gm_t88rcucg1b7uk t114 = gm_l17;
                        gm_tkn42u7na3t81 t115;
                        GM_ST(uint8_t, (uint8_t *)&t115 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t115 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t115), t114);
                        ((uint8_t *)&t115)[4] = 1;
                        gm_l14 = t115;
                        goto gm_lb18;
                    }
gm_lb19:;
                }
gm_lb18:;
            }
gm_lb14:;
            {
                {
                    uint8_t t116 = (uint8_t)(((((uint8_t *)&gm_l13) + 8))[16] != 0);
                    if (!t116) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                    gm_l18 = t117;
                    gm_l20 = ((uint64_t)0ULL);
                    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    gm_l19 = t118;
                    {
                        for (;;) {
                            uint64_t t119 = gm_l20;
                            uint64_t t120 = gm_l19;
                            uint8_t t121 = (t119 < t120);
                            if (!t121) {
                                goto gm_lb22;
                            }
                            {
                                {
                                    {
                                        uint64_t t122 = gm_l20;
                                        gm_l21 = t122;
                                        void * t123 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                        uint64_t t125 = gm_l21;
                                        void * t126 = (void *)(((uint8_t *)t123) + (uintptr_t)t125 * 120);
                                        gm_l22 = t126;
                                        gm_tkn42u7na3t81 t127;
                                        memset(&t127, 0, sizeof t127);
                                        gm_l23 = t127;
                                        {
                                            {
                                                void * t128 = gm_l22;
                                                uint8_t t129 = (uint8_t)((((uint8_t *)t128))[32] != 0);
                                                if (!t129) {
                                                    goto gm_lb28;
                                                }
                                                memmove(((uint8_t *)&gm_l24), ((uint8_t *)t128), 32);
                                                {
                                                    {
                                                        uint8_t t130 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                                                        if (!t130) {
                                                            goto gm_lb30;
                                                        }
                                                        gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
                                                        gm_l25 = t131;
                                                        void * t132 = gm_l0;
                                                        gm_t88rcucg1b7uk t133 = gm_l25;
                                                        uint8_t t134 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t132, t133, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l24)));
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb29;
                                                    }
gm_lb30:;
                                                }
gm_lb29:;
                                                goto gm_lb27;
                                            }
gm_lb28:;
                                        }
gm_lb27:;
                                        {
                                            {
                                                void * t135 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                                uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t135) + 8) + 0));
                                                uint8_t t137 = (uint8_t)(t136 == ((uint8_t)8ULL));
                                                if (!t137) {
                                                    goto gm_lb32;
                                                }
                                                memmove(((uint8_t *)&gm_l26), ((((uint8_t *)t135) + 8) + 8), 32);
                                                {
                                                    {
                                                        uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
                                                        if (!t138) {
                                                            goto gm_lb34;
                                                        }
                                                        gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                                                        gm_l27 = t139;
                                                        void * t140 = gm_l0;
                                                        gm_t88rcucg1b7uk t141 = gm_l27;
                                                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                                                        gm_tkn42u7na3t81 t143 = gm_f_variant_payload_3id0koh(t140, GM_CONV(gm_csguankko4lmhd, t141), t142);
                                                        gm_l23 = t143;
                                                        goto gm_lb33;
                                                    }
gm_lb34:;
                                                }
gm_lb33:;
                                                goto gm_lb31;
                                            }
gm_lb32:;
                                        }
gm_lb31:;
                                        void * t144 = gm_l0;
                                        void * t145 = gm_l22;
                                        void * t146 = (void *)(((uint8_t *)t145) + 40);
                                        gm_tkn42u7na3t81 t147 = gm_l23;
                                        uint32_t t148 = gm_l3;
                                        gm_b0_1 t149 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t144, t146, t147, t148);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 780, 17);
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint64_t t150 = gm_l20;
                            uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                            uint64_t t153 = r151;
                            gm_l20 = t153;
                        }
                    }
gm_lb22:;
                    goto gm_lb20;
                }
gm_lb21:;
            }
gm_lb20:;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t155 = (uint8_t)(t154 == ((uint8_t)8ULL));
            if (!t155) {
                goto gm_lb35;
            }
            gm_b16_8 t156 = GM_LD(gm_b16_8, ((((uint8_t *)t42) + 8) + 8));
            gm_l28 = t156;
            gm_l30 = ((uint64_t)0ULL);
            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
            gm_l29 = t157;
            {
                for (;;) {
                    uint64_t t158 = gm_l30;
                    uint64_t t159 = gm_l29;
                    uint8_t t160 = (t158 < t159);
                    if (!t160) {
                        goto gm_lb36;
                    }
                    {
                        {
                            {
                                uint64_t t161 = gm_l30;
                                gm_l31 = t161;
                                void * t162 = gm_l0;
                                void * t163 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                uint64_t t165 = gm_l31;
                                void * t166 = (void *)((((uint8_t *)t163) + (uintptr_t)t165 * 120) + 40);
                                gm_tkn42u7na3t81 t167;
                                memset(&t167, 0, sizeof t167);
                                uint32_t t168 = gm_l3;
                                gm_b0_1 t169 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t162, t166, t167, t168);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb39;
                            }
gm_lb40:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 794, 13);
                        }
gm_lb39:;
                    }
gm_lb38:;
                    uint64_t t170 = gm_l30;
                    uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                    uint64_t t173 = r171;
                    gm_l30 = t173;
                }
            }
gm_lb36:;
            goto gm_lb1;
        }
gm_lb35:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 753, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_payload_field_3o532rn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_payload_field_3o532rn F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* variant */
    gm_t08olq97rscn3 gm_l3; /* field */
    void * gm_l4; /* decl */
    void * gm_l6; /* lit */
    gm_toceffb697p0u gm_l7; /* found */
    gm_tl874t2atnlh3 gm_l8; /* to */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = GM_LD(void *, (((uint8_t *)t10) + 240));
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 240) + 8));
    uint64_t t13 = gm_l1;
    if ((uintptr_t)t13 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t13, t12, gm_srcb2hdor4f6g0q, 421, 14);
    void * t14 = (void *)(((uint8_t *)t11) + (uintptr_t)t13 * 216);
    gm_l4 = t14;
    {
        {
            void * t15 = gm_l4;
            uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)t15) + 40) + 64) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
            if (!t17) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), (((((uint8_t *)t15) + 40) + 64) + 8), 64);
            {
                {
                    void * t18 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)33ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    void * t21 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 8));
                    gm_l6 = t21;
                    gm_toceffb697p0u t22;
                    memset(&t22, 0, sizeof t22);
                    gm_l7 = t22;
                    void * t23 = gm_l6;
                    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 48));
                    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                    gm_b16_8 t26 = gm_l2;
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)a3));
                    void * t28 = (void *)((uint8_t *)&gm_l7);
                    gm_f_find_payload_field_i16vrf1(t25, t26, t27, t28);
                    {
                        {
                            uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l7))[12] != 0);
                            if (!t29) {
                                goto gm_lb6;
                            }
                            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l7));
                            gm_l8 = t30;
                            void * t31 = gm_l0;
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                            gm_tl874t2atnlh3 t33 = gm_l8;
                            gm_b0_1 t34 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t31, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
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
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
extern __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3_c(void * a0, gm_t1s75i08qtptd a1) { gm_f_pop_label_e8oj3h3(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_gg2j5c3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_label_gg2j5c3 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&F.l1))[32] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l2), ((uint8_t *)&F.l1), 32);
            }
            {
                void * t5 = gm_l0;
                void * t6 = (void *)(((uint8_t *)t5) + 72);
                gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                gm_t08olq97rscn3 t9;
                GM_ST(uint32_t, (uint8_t *)&t9 + 28, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t9 + 16), t8);
                gm_tftb1odc739t5 t10 = gm_f_push_kiclmcn_2(t6, &t9);
                gm_l3 = t10;
            }
            uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcb2hdor4f6g0q, 726, 9);
            {
                void * t12 = gm_l0;
                gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_tnpifv20irnph t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
                gm_f_format_ekcps7q((void *)&gm_l4, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcb2hdor4f6g0q, 727, 40);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_th2lqbo77pvsc t19;
                memset(&t19, 0, sizeof t19);
                gm_t6ok5tvcjc200 t20;
                GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t18);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t20 + 16), t19);
                gm_b0_1 t21 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t12, t13, &t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ref_key_vam5ppg F;
    gm_tsncjtib3j1l1 gm_l0; /* d */
    uint64_t gm_l1; /* x */
    uint64_t gm_l2; /* x */
    uint32_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t gm_l4;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            uint64_t t8 = gm_l1;
            uint64_t t9 = (uint64_t)t8;
            gm_l4 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l2 = t12;
            uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)a0) + 8) + 8));
            gm_l3 = t13;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)60ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcb2hdor4f6g0q, 83, 19);
            uint64_t r15 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != ((uint64_t)1ULL);
            if (o16) gm_trap("integer overflow in `<<`", gm_srcb2hdor4f6g0q, 83, 19);
            uint64_t t17 = r15;
            uint64_t t18 = gm_l2;
            uint64_t t19 = (uint64_t)t18;
            unsigned __int128 n20 = (unsigned __int128)((uint64_t)16ULL);
            if (n20 >= 64) gm_trap_shift((uint64_t)n20, "u64", gm_srcb2hdor4f6g0q, 83, 36);
            uint64_t r21 = (uint64_t)((uint64_t)t19 << (int)n20);
            bool o22 = (uint64_t)(r21 >> (int)n20) != t19;
            if (o22) gm_trap("integer overflow in `<<`", gm_srcb2hdor4f6g0q, 83, 36);
            uint64_t t23 = r21;
            uint64_t t24 = (uint64_t)(t17 | t23);
            uint32_t t25 = gm_l3;
            uint64_t t26 = (uint64_t)t25;
            uint64_t t27 = (uint64_t)(t24 | t26);
            gm_l4 = t27;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 81, 37);
    }
gm_lb1:;
    uint64_t t28 = gm_l4;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg_c(gm_tsncjtib3j1l1 a0) { return gm_f_ref_key_vam5ppg(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_ald930b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_ald930b F;
    void * gm_l0; /* w */
    void * gm_l1; /* s */
    gm_b16_8 gm_l3; /* places */
    void * gm_l4; /* value */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* e */
    void * gm_l9; /* e */
    void * gm_l10; /* body */
    void * gm_l11; /* x */
    void * gm_l14; /* value */
    void * gm_l15; /* fallback */
    gm_tkn42u7na3t81 gm_l16; /* ty */
    GM_STACK_CHECK(&F, 591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t17 = gm_l1;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((((uint8_t *)t17) + 8) + 8) + 16), 64);
            void * t20 = gm_l0;
            void * t21 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t22 = gm_f_local_binding_25t1g13((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)1ULL));
            if (!t24) {
                goto gm_lb3;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 8) + 8));
            gm_l3 = t25;
            void * t26 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 24));
            gm_l4 = t26;
            gm_l6 = ((uint64_t)0ULL);
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l5 = t27;
            {
                for (;;) {
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = gm_l5;
                    uint8_t t30 = (t28 < t29);
                    if (!t30) {
                        goto gm_lb4;
                    }
                    {
                        {
                            {
                                uint64_t t31 = gm_l6;
                                gm_l7 = t31;
                                void * t32 = gm_l0;
                                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t35 = gm_l7;
                                void * t36 = (void *)(((uint8_t *)t33) + (uintptr_t)t35 * 88);
                                gm_b0_1 t37 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t32, t36);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 612, 13);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t38 = gm_l6;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l6 = t41;
                }
            }
gm_lb4:;
            void * t42 = gm_l0;
            void * t43 = gm_l4;
            gm_b0_1 t44 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
            if (!t46) {
                goto gm_lb9;
            }
            void * t47 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l8 = t47;
            void * t48 = gm_l0;
            void * t49 = gm_l8;
            gm_b0_1 t50 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)6ULL));
            if (!t52) {
                goto gm_lb10;
            }
            void * t53 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l9 = t53;
            void * t54 = gm_l0;
            void * t55 = gm_l9;
            gm_b0_1 t56 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)3ULL));
            if (!t58) {
                goto gm_lb11;
            }
            void * t59 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 8));
            gm_l10 = t59;
            void * t60 = gm_l0;
            void * t61 = gm_l10;
            gm_b0_1 t62 = gm_f_stmt_ald930b((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)4ULL));
            if (!t64) {
                goto gm_lb12;
            }
            void * t65 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l11 = t65;
            void * t66 = gm_l0;
            void * t67 = gm_l11;
            gm_b0_1 t68 = gm_f_when_stmt_r5g5fsm((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t70 = (uint8_t)(t69 == ((uint8_t)5ULL));
            if (!t70) {
                goto gm_lb13;
            }
            {
                memmove(((uint8_t *)&F.l12), ((((uint8_t *)t17) + 8) + 8), 112);
            }
            void * t71 = gm_l0;
            uint32_t t72 = GM_LD(uint32_t, (((((uint8_t *)&F.l12) + 72) + 24) + 8));
            gm_b0_1 t73 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t74 = gm_l0;
            void * t75 = (void *)(((uint8_t *)&F.l12) + 40);
            gm_b0_1 t76 = gm_f_cond_d94lif5((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t77 = gm_l0;
                gm_b0_1 t78 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t77, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t79 = gm_l0;
            void * t80 = (void *)(((uint8_t *)&F.l12) + 72);
            gm_b0_1 t81 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t82 = gm_l0;
                gm_f_pop_label_e8oj3h3(t82, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l12)));
            }
            void * t83 = gm_l0;
            gm_f_pop_scope_vgka5ae(t83);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)7ULL));
            if (!t85) {
                goto gm_lb14;
            }
            memmove(((uint8_t *)&F.l13), ((((uint8_t *)t17) + 8) + 8), 64);
            void * t86 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 64));
            gm_l14 = t86;
            void * t87 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 72));
            gm_l15 = t87;
            void * t88 = gm_l0;
            void * t89 = gm_l14;
            gm_b0_1 t90 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t91 = gm_l0;
            void * t92 = gm_l15;
            gm_b0_1 t93 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t94 = gm_l0;
            void * t95 = gm_l14;
            gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 72));
            gm_tkn42u7na3t81 t97 = gm_f_type_of_tiqb21n(t94, GM_CONV(gm_cseahvg8v7p0vs, t96));
            gm_l16 = t97;
            void * t98 = gm_l0;
            void * t99 = (void *)((uint8_t *)&F.l13);
            gm_tkn42u7na3t81 t100 = gm_l16;
            void * t101 = gm_l1;
            uint32_t t102 = GM_LD(uint32_t, ((((uint8_t *)t101) + 128) + 8));
            gm_b0_1 t103 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t98, t99, t100, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)8ULL));
            if (!t105) {
                goto gm_lb15;
            }
            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t17) + 8) + 8), 40);
            void * t106 = gm_l0;
            void * t107 = (void *)((uint8_t *)&F.l17);
            gm_b0_1 t108 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 609, 5);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_tiqb21n(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_of_tiqb21n F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkn42u7na3t81 gm_l2; /* t */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tpk1phrp285m3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 8));
            void * t6 = (void *)((uint8_t *)t5);
            gm_tl874t2atnlh3 t7 = gm_l1;
            gm_tpk1phrp285m3 t8 = gm_f_get_o4mipv6(t6, GM_CONV(gm_cseahvg8v7p0vs, t7));
            gm_l3 = t8;
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_tkn42u7na3t81 t10 = GM_LD(gm_tkn42u7na3t81, ((uint8_t *)&gm_l3));
            gm_l2 = t10;
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
            gm_tkn42u7na3t81 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[4] = 1;
            F.ret = t12;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tkn42u7na3t81 t13;
    memset(&t13, 0, sizeof t13);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_use_decl_sfao4j5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_use_decl_sfao4j5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    gm_tsncjtib3j1l1 gm_l2; /* d */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = gm_l0;
        gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_decl_name_span_ht2nhb5(t4, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2))));
        gm_b0_1 t6 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        gm_b0_1 t8 = gm_f_ensure_hover_3epdjmk((void *)NULL, cx, NULL, t7, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_b0_1 t11 = gm_f_note_receiver_6mb4kc5((void *)NULL, cx, NULL, t9, t10, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_at_9i78oa0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_variant_at_9i78oa0 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 391u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqetrgo0q24b5 gm_l8;
    gm_tf5srh4mp4b3i gm_l9;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
    void * t11 = (void *)(((uint8_t *)t10) + 56);
    gm_tl874t2atnlh3 t12 = gm_l1;
    gm_f_get_b2m4si4((void *)&gm_l8, t11, GM_CONV(gm_cseahvg8v7p0vs, t12));
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l8))[40] != 0);
    if (t13) {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l8), 40);
    } else {
        gm_tf5srh4mp4b3i t14;
        memset(&t14, 0, sizeof t14);
        F.ret = t14;
        goto gm_fret;
    }
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
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
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                        gm_l6 = t22;
                        {
                            {
                                void * t23 = gm_l0;
                                gm_t88rcucg1b7uk t24 = gm_l6;
                                gm_tf5srh4mp4b3i t25 = gm_f_variant_use_29ooua3((void *)NULL, cx, NULL, t23, t24, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l9 = t25;
                                uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                                gm_l7 = t27;
                                uint64_t t28 = gm_l7;
                                gm_tf5srh4mp4b3i t29;
                                GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t29), t28);
                                ((uint8_t *)&t29)[8] = 1;
                                F.ret = t29;
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 393, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l5;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l5 = t33;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t34;
    memset(&t34, 0, sizeof t34);
    F.ret = t34;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_variant_payload_3id0koh(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_variant_payload_3id0koh F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* variants */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l11;
    gm_t53at80pfdus3 gm_l12;
    gm_t88rcucg1b7uk t18 = gm_l1;
    gm_l3 = t18;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t19 = gm_l0;
                void * t20 = GM_LD(void *, ((uint8_t *)t19));
                void * t21 = (void *)(((uint8_t *)t20) + 8);
                gm_t88rcucg1b7uk t22 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l11, t21, GM_CONV(gm_csguankko4lmhd, t22));
                {
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                        uint8_t t24 = (uint8_t)(t23 == ((uint8_t)13ULL));
                        if (!t24) {
                            goto gm_lb5;
                        }
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 8));
                        gm_l4 = t25;
                        gm_t88rcucg1b7uk t26 = gm_l4;
                        gm_l3 = t26;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                        uint8_t t28 = (uint8_t)(t27 == ((uint8_t)8ULL));
                        if (!t28) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 4));
                        gm_l5 = t29;
                        gm_t88rcucg1b7uk t30 = gm_l5;
                        gm_l3 = t30;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        goto gm_lb1;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 824, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, ((uint8_t *)t31));
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            gm_t88rcucg1b7uk t34 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l12, t33, GM_CONV(gm_csguankko4lmhd, t34));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)17ULL));
            if (!t36) {
                goto gm_lb9;
            }
            uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
            gm_l6 = t37;
            {
                {
                    void * t38 = gm_l0;
                    void * t39 = GM_LD(void *, ((uint8_t *)t38));
                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 1088));
                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 1088) + 8));
                    uint32_t t42 = gm_l6;
                    uint64_t t43 = (uint64_t)t42;
                    if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcb2hdor4f6g0q, 831, 28);
                    uint8_t t44 = GM_LD(uint8_t, (((((uint8_t *)t40) + (uintptr_t)t43 * 184) + 24) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)1ULL));
                    if (!t45) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((((((uint8_t *)t40) + (uintptr_t)t43 * 184) + 24) + 8) + 8));
                    gm_l7 = t46;
                    gm_b16_8 t47 = gm_l7;
                    gm_l8 = t47;
                    gm_l9 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t48 = gm_l9;
                            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t50 = (t48 < t49);
                            if (!t50) {
                                goto gm_lb12;
                            }
                            {
                                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint64_t t53 = gm_l9;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l10), (((uint8_t *)t51) + (uintptr_t)t53 * 72), 72);
                                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t55 = gm_l2;
                                        uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
                                        if (t56) {
                                            gm_tkn42u7na3t81 t57 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l10) + 16));
                                            F.ret = t57;
                                            goto gm_fret;
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 832, 5);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t58 = gm_l9;
                            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                            uint64_t t61 = r59;
                            gm_l9 = t61;
                        }
                    }
gm_lb12:;
                    gm_tkn42u7na3t81 t62;
                    memset(&t62, 0, sizeof t62);
                    F.ret = t62;
                    goto gm_fret;
                }
gm_lb11:;
                gm_tkn42u7na3t81 t63;
                memset(&t63, 0, sizeof t63);
                F.ret = t63;
                goto gm_fret;
            }
gm_lb10:;
        }
gm_lb9:;
        gm_tkn42u7na3t81 t64;
        memset(&t64, 0, sizeof t64);
        F.ret = t64;
        goto gm_fret;
    }
gm_lb8:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_use_29ooua3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_variant_use_29ooua3 F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint64_t gm_l7; /* d */
    gm_tl874t2atnlh3 gm_l8; /* to */
    GM_STACK_CHECK(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    gm_toceffb697p0u gm_l11;
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_l3 = t14;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, ((uint8_t *)t15));
                void * t17 = (void *)(((uint8_t *)t16) + 8);
                gm_t88rcucg1b7uk t18 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l9, t17, GM_CONV(gm_csguankko4lmhd, t18));
                {
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                        uint8_t t20 = (uint8_t)(t19 == ((uint8_t)13ULL));
                        if (!t20) {
                            goto gm_lb5;
                        }
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
                        gm_l4 = t21;
                        gm_t88rcucg1b7uk t22 = gm_l4;
                        gm_l3 = t22;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                        uint8_t t24 = (uint8_t)(t23 == ((uint8_t)14ULL));
                        if (!t24) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l9) + 8) + 4));
                        gm_l5 = t25;
                        gm_t88rcucg1b7uk t26 = gm_l5;
                        gm_l3 = t26;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                        uint8_t t28 = (uint8_t)(t27 == ((uint8_t)8ULL));
                        if (!t28) {
                            goto gm_lb7;
                        }
                        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l9) + 8) + 4));
                        gm_l6 = t29;
                        gm_t88rcucg1b7uk t30 = gm_l6;
                        gm_l3 = t30;
                        goto gm_lb4;
                    }
gm_lb7:;
                    {
                        goto gm_lb1;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 403, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, ((uint8_t *)t31));
            gm_t88rcucg1b7uk t33 = gm_l3;
            gm_tf5srh4mp4b3i t34 = gm_f_adt_decl_of_2bflsov(t32, GM_CONV(gm_csguankko4lmhd, t33));
            gm_l10 = t34;
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
            if (!t35) {
                goto gm_lb10;
            }
            uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
            gm_l7 = t36;
            {
                {
                    void * t37 = gm_l0;
                    uint64_t t38 = gm_l7;
                    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_toceffb697p0u t40 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t37, t38, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t40;
                    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l11))[12] != 0);
                    if (!t41) {
                        goto gm_lb12;
                    }
                    gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l11));
                    gm_l8 = t42;
                    void * t43 = gm_l0;
                    gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                    gm_tl874t2atnlh3 t45 = gm_l8;
                    gm_b0_1 t46 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t43, t44, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t47 = gm_l7;
                    gm_tf5srh4mp4b3i t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t48), t47);
                    ((uint8_t *)&t48)[8] = 1;
                    F.ret = t48;
                    goto gm_fret;
                }
gm_lb12:;
            }
gm_lb11:;
            goto gm_lb9;
        }
gm_lb10:;
    }
gm_lb9:;
    gm_tf5srh4mp4b3i t49;
    memset(&t49, 0, sizeof t49);
    F.ret = t49;
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
__attribute__((aligned(4))) void gm_f_walker_8qtbjaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3) {
    struct gm_fr_walker_8qtbjaj F;
    void * gm_l0; /* s */
    void * gm_l1; /* facts */
    void * gm_l2; /* idx */
    uint64_t gm_l3; /* module */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l9; /* f */
    uint64_t gm_l10; /* p */
    uint64_t gm_l11; /* yd */
    void * gm_l12; /* lit */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    GM_STACK_CHECK(&F, 1481u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tagrkbqh054h7 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tf5srh4mp4b3i gm_l18;
    void * gm_l19;
    {
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        void * t24 = gm_l2;
        uint64_t t25 = gm_l3;
        gm_trqfvmjtt4sij t26;
        gm_f_list_ubba23m_2((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t27;
        gm_f_list_rk274m7_2((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t28;
        gm_f_hash_map_7pa51u7((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t29;
        gm_f_hash_map_oh3lgki((void *)&t29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t30;
        gm_f_hash_map_s7cd6p5((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1335qftqvd8f t31;
        memset(&t31, 0, sizeof t31);
        gm_tt5bocau6bbvj t32;
        GM_ST(void *, ((uint8_t *)&t32 + 0), t22);
        GM_ST(void *, ((uint8_t *)&t32 + 8), t23);
        GM_ST(void *, ((uint8_t *)&t32 + 16), t24);
        GM_ST(uint64_t, ((uint8_t *)&t32 + 24), t25);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 32), t26);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 72), t27);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t32 + 112), t28);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t32 + 168), t29);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t32 + 224), t30);
        GM_ST(gm_t1335qftqvd8f, ((uint8_t *)&t32 + 280), t31);
        F.l4 = t32;
    }
    gm_l6 = ((uint64_t)0ULL);
    void * t33 = gm_l0;
    void * t34 = GM_LD(void *, ((uint8_t *)t33));
    uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t34) + 240) + 8));
    gm_l5 = t35;
    {
        for (;;) {
            uint64_t t36 = gm_l6;
            uint64_t t37 = gm_l5;
            uint8_t t38 = (t36 < t37);
            if (!t38) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t39 = gm_l6;
                        gm_l7 = t39;
                        {
                            {
                                {
                                    void * t40 = gm_l0;
                                    uint64_t t41 = gm_l7;
                                    gm_f_state_a5bnsjb((void *)&gm_l16, (void *)NULL, cx, NULL, t40, t41);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)2ULL));
                                if (!t43) {
                                    goto gm_lb7;
                                }
                                {
                                    memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l16) + 8), 208);
                                }
                                {
                                    {
                                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                        uint8_t t45 = (uint8_t)(t44 == ((uint8_t)5ULL));
                                        if (!t45) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 8));
                                        gm_l9 = t46;
                                        void * t47 = (void *)(((uint8_t *)&F.l4) + 224);
                                        uint32_t t48 = gm_l9;
                                        uint8_t t49 = gm_f_contains_2dnkft5(t47, t48);
                                        uint8_t t50 = (uint8_t)!t49;
                                        if (t50) {
                                            void * t51 = (void *)(((uint8_t *)&F.l4) + 224);
                                            uint32_t t52 = gm_l9;
                                            uint64_t t53 = gm_l7;
                                            gm_tftb1odc739t5 t54 = gm_f_put_sokl06f(t51, t52, t53);
                                            gm_l17 = t54;
                                            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcb2hdor4f6g0q, 102, 49);
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 99, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t56 = gm_l6;
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            uint64_t t59 = r57;
            gm_l6 = t59;
        }
    }
gm_lb1:;
    {
        {
            void * t60 = gm_l0;
            void * t61 = GM_LD(void *, ((uint8_t *)t60));
            uint8_t t62 = (uint8_t)(((((uint8_t *)t61) + 320))[8] != 0);
            if (!t62) {
                goto gm_lb11;
            }
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t61) + 320));
            gm_l10 = t63;
            {
                {
                    void * t64 = gm_l0;
                    void * t65 = GM_LD(void *, ((uint8_t *)t64));
                    uint64_t t66 = gm_l10;
                    gm_b16_8 t67;
                    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_ses7tdrdt8a6u));
                    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(5));
                    gm_tf5srh4mp4b3i t68 = gm_f_lookup_59juqoo(t65, t66, t67);
                    gm_l18 = t68;
                    uint8_t t69 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
                    if (!t69) {
                        goto gm_lb13;
                    }
                    uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                    gm_l11 = t70;
                    void * t71 = gm_l0;
                    void * t72 = GM_LD(void *, ((uint8_t *)t71));
                    void * t73 = GM_LD(void *, (((uint8_t *)t72) + 240));
                    uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t72) + 240) + 8));
                    uint64_t t75 = gm_l11;
                    if ((uintptr_t)t75 >= (uintptr_t)(t74)) gm_trap_index((uintptr_t)t75, t74, gm_srcb2hdor4f6g0q, 108, 16);
                    gm_t9qkh0rsooc8d t76 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t73) + (uintptr_t)t75 * 216) + 40) + 56));
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                    uint8_t t78 = (uint8_t)((t77 == ((uint8_t)2ULL)));
                    gm_t9qkh0rsooc8d t79;
                    GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)2ULL));
                    if (t78) {
                        {
                            {
                                void * t80 = gm_l0;
                                uint64_t t81 = gm_l11;
                                void * t82 = gm_f_effect_lit_26tkk47(t80, t81);
                                gm_l19 = t82;
                                uint8_t t83 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l19)) != 0);
                                if (!t83) {
                                    goto gm_lb15;
                                }
                                void * t84 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                gm_l12 = t84;
                                gm_l14 = ((uint64_t)0ULL);
                                void * t85 = gm_l12;
                                uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)t85) + 24) + 8));
                                gm_l13 = t86;
                                {
                                    for (;;) {
                                        uint64_t t87 = gm_l14;
                                        uint64_t t88 = gm_l13;
                                        uint8_t t89 = (t87 < t88);
                                        if (!t89) {
                                            goto gm_lb16;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t90 = gm_l14;
                                                    gm_l15 = t90;
                                                    void * t91 = gm_l12;
                                                    void * t92 = GM_LD(void *, (((uint8_t *)t91) + 24));
                                                    uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 24) + 8));
                                                    uint64_t t94 = gm_l15;
                                                    if ((uintptr_t)t94 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t94, t93, gm_srcb2hdor4f6g0q, 111, 28);
                                                    gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)t92) + (uintptr_t)t94 * 80));
                                                    gm_b16_8 t96;
                                                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s4g2k7ups3ui6));
                                                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(5));
                                                    uint8_t t97 = (gm_eq0iqiubfm7e(((uint8_t *)&t95), ((uint8_t *)&t96)));
                                                    if (t97) {
                                                        {
                                                            uint64_t t98 = gm_l11;
                                                            uint64_t t99 = gm_l15;
                                                            uint32_t t100 = (uint32_t)t99;
                                                            gm_tn68el8d14vku t101;
                                                            GM_ST(uint32_t, (uint8_t *)&t101 + 12, 0);
                                                            GM_ST(uint64_t, ((uint8_t *)&t101 + 0), t98);
                                                            GM_ST(uint32_t, ((uint8_t *)&t101 + 8), t100);
                                                            gm_tsncjtib3j1l1 t102;
                                                            GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                                                            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)1ULL));
                                                            GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t102 + 8), t101);
                                                            gm_t1335qftqvd8f t103;
                                                            GM_ST(uint8_t, (uint8_t *)&t103 + 25, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t103 + 26, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t103 + 28, 0);
                                                            GM_ST(gm_tsncjtib3j1l1, ((uint8_t *)&t103), t102);
                                                            ((uint8_t *)&t103)[24] = 1;
                                                            GM_ST(gm_t1335qftqvd8f, (((uint8_t *)&F.l4) + 280), t103);
                                                        }
                                                        goto gm_lb16;
                                                    }
                                                    goto gm_lb19;
                                                }
gm_lb20:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 110, 21);
                                            }
gm_lb19:;
                                        }
gm_lb18:;
                                        uint64_t t104 = gm_l14;
                                        uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                                        uint64_t t107 = r105;
                                        gm_l14 = t107;
                                    }
                                }
gm_lb16:;
                                goto gm_lb14;
                            }
gm_lb15:;
                        }
gm_lb14:;
                    }
                    goto gm_lb12;
                }
gm_lb13:;
            }
gm_lb12:;
            goto gm_lb10;
        }
gm_lb11:;
    }
gm_lb10:;
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 312);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_r5g5fsm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_r5g5fsm F;
    void * gm_l0; /* w */
    void * gm_l1; /* x */
    void * gm_l4; /* x2 */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    void * t8 = gm_l1;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    gm_b0_1 t10 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t7, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = gm_l0;
    void * t12 = gm_l1;
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_b0_1 t14 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t11, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t15 = gm_l1;
            uint8_t t16 = (uint8_t)(((((uint8_t *)t15) + 48))[48] != 0);
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t15) + 48), 48);
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 8), 40);
                    void * t19 = gm_l0;
                    void * t20 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t21 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t19, t20);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)1ULL));
                    if (!t23) {
                        goto gm_lb5;
                    }
                    void * t24 = GM_LD(void *, (((uint8_t *)&F.l2) + 8));
                    gm_l4 = t24;
                    void * t25 = gm_l0;
                    void * t26 = gm_l4;
                    gm_b0_1 t27 = gm_f_when_stmt_r5g5fsm((void *)NULL, cx, NULL, t25, t26);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 641, 9);
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

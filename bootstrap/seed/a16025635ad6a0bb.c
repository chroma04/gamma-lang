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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_region_text_snb7ngp {
    gm_frame h;
    gm_tin97tt4ud0s1 l7; /* view */
    gm_trqfvmjtt4sij l8; /* order */
    gm_trqfvmjtt4sij l15; /* out */
    gm_trqfvmjtt4sij l21; /* labels */
    gm_t4gut2c9fj1kt l26; /* nd */
    gm_b16_8 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remap_qm0iknp {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_render_region_ppot3lm {
    gm_frame h;
    gm_tjt4ka19str13 l6; /* a */
    gm_b16_8 ret;
    gm_b16_8 w0;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_trap_report_a7hiu65 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_a8h402l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_diqdb80 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rcan3t8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq8l669blqfv(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_u31m55g(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_region_text_snb7ngp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_remap_qm0iknp(gm_cseahvg8v7p0vs a0, uint32_t a1, uint32_t a2, uint32_t a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_trap_report_a7hiu65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_a8h402l(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_diqdb80(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fd01t33(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lldn1qi(void * a0, gm_tcc1rq4n1avb2 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rcan3t8(void * a0, gm_tnpifv20irnph a1);
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_kold22d1smp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s9hdvuc1np1st[10];
extern const uint8_t gm_sb3u9q4alu9bg[7];
extern const uint8_t gm_scggaq5euuhe7[19];
extern const uint8_t gm_sdsg5rjte31pl[15];
extern const uint8_t gm_smos9ggka3uc3[10];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2n2ugogq2cq2[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const gm_tmpl gm_tpl29spsi1atllj;
extern const gm_tmpl gm_tpljkecvj49gp71;
extern const gm_tmpl gm_tplt524tdio8698;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcv1nri2247d2r, 82, 75);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 80);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jbu9fo3();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_bvkp9ir((void *)&t2, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_a8h402l(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_a8h402l F;
    void * gm_l0; /* l */
    gm_t5hdnnftkup6m gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsg5rjte31pl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_tftb1odc739t5 t4 = gm_f_write_fd01t33(t2, &(*(gm_t5hdnnftkup6m *)((uint8_t *)a1)), ((void *)&gm_tpl29spsi1atllj), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_diqdb80(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_diqdb80 F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tcc1rq4n1avb2 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_scggaq5euuhe7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(18));
    gm_tftb1odc739t5 t5 = gm_f_write_lldn1qi(t2, t3, ((void *)&gm_tpljkecvj49gp71), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rcan3t8(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rcan3t8 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s9hdvuc1np1st));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplt524tdio8698), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) < GM_LD(uint32_t, b);
}
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return gm_lt467d5cm1d5(a + 0, b + 0);
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return gm_lt467d5cm1d5(a + 4, b + 4);
    return false;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_region_text_snb7ngp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5) {
    struct gm_fr_region_text_snb7ngp F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    uint32_t gm_l2; /* file */
    uint32_t gm_l3; /* lo */
    gm_b16_8 gm_l4; /* input */
    gm_t9qkh0rsooc8d gm_l5; /* format */
    uint32_t gm_l6; /* hi */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_tcc1rq4n1avb2 gm_l12; /* k */
    uint64_t gm_l13; /* at */
    gm_tl874t2atnlh3 gm_l14; /* p */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    void * gm_l19; /* d */
    uint8_t gm_l20; /* inside */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_tbgd14esf0jvi gm_l24; /* l */
    gm_tl874t2atnlh3 gm_l25; /* s */
    GM_STACK_CHECK(&F, 1900u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    gm_toceffb697p0u gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tl874t2atnlh3 gm_l33;
    gm_toceffb697p0u gm_l34;
    {
        uint32_t t24 = gm_l3;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint32_t t26 = (uint32_t)t25;
        uint32_t r27; bool o28 = __builtin_add_overflow(t24, t26, &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_src2n2ugogq2cq2, 24, 11);
        uint32_t t29 = r27;
        gm_l6 = t29;
        gm_tin97tt4ud0s1 t30;
        gm_f_source_map_4v8dg4b((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t30;
        void * t31 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sb3u9q4alu9bg));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(6));
        gm_b16_8 t33 = gm_l4;
        uint32_t t34 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t35 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_smos9ggka3uc3));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(9));
        void * t37 = gm_l0;
        uint32_t t38 = gm_l2;
        void * t39 = gm_f_file_o15fqku(t37, t38);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t39) + 16));
        uint32_t t41 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t35, t36, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t42;
        gm_f_list_5dhgg4i((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t42;
        gm_l10 = ((uint64_t)0ULL);
        void * t43 = gm_l1;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 8));
        gm_l9 = t44;
        {
            for (;;) {
                uint64_t t45 = gm_l10;
                uint64_t t46 = gm_l9;
                uint8_t t47 = (t45 < t46);
                if (!t47) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t48 = gm_l10;
                            gm_l11 = t48;
                            void * t49 = gm_l1;
                            void * t50 = GM_LD(void *, ((uint8_t *)t49));
                            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t49) + 8));
                            uint64_t t52 = gm_l11;
                            if ((uintptr_t)t52 >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)t52, t51, gm_src2n2ugogq2cq2, 31, 15);
                            uint32_t t53 = GM_LD(uint32_t, ((((uint8_t *)t50) + (uintptr_t)t52 * 184) + 24));
                            void * t54 = gm_l1;
                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
                            uint64_t t57 = gm_l11;
                            if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_src2n2ugogq2cq2, 31, 46);
                            uint32_t t58 = GM_LD(uint32_t, (((((uint8_t *)t55) + (uintptr_t)t57 * 184) + 24) + 4));
                            gm_tcc1rq4n1avb2 t59;
                            GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t53);
                            GM_ST(uint32_t, ((uint8_t *)&t59 + 4), t58);
                            gm_l12 = t59;
                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            gm_l13 = t60;
                            {
                                for (;;) {
                                    uint64_t t61 = gm_l13;
                                    uint8_t t62 = (t61 > ((uint64_t)0ULL));
                                    if (!t62) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t63 = gm_l1;
                                        void * t64 = GM_LD(void *, ((uint8_t *)t63));
                                        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t63) + 8));
                                        void * t66 = GM_LD(void *, ((uint8_t *)&F.l8));
                                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                        uint64_t t68 = gm_l13;
                                        uint64_t r69; bool o70 = __builtin_sub_overflow(t68, ((uint64_t)1ULL), &r69);
                                        if (o70) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 34, 47);
                                        uint64_t t71 = r69;
                                        if ((uintptr_t)t71 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t71, t67, gm_src2n2ugogq2cq2, 34, 35);
                                        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t66) + (uintptr_t)t71 * 8));
                                        if ((uintptr_t)t72 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t72, t65, gm_src2n2ugogq2cq2, 34, 18);
                                        gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t64) + (uintptr_t)t72 * 184) + 24));
                                        gm_l14 = t73;
                                        uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                                        gm_tcc1rq4n1avb2 t76;
                                        GM_ST(uint32_t, ((uint8_t *)&t76 + 0), t74);
                                        GM_ST(uint32_t, ((uint8_t *)&t76 + 4), t75);
                                        gm_tcc1rq4n1avb2 t77 = gm_l12;
                                        uint8_t t78 = (uint8_t)(gm_lt8l669blqfv(((uint8_t *)&t76), ((uint8_t *)&t77)) || gm_eq8l669blqfv(((uint8_t *)&t76), ((uint8_t *)&t77)));
                                        if (t78) {
                                            goto gm_lb6;
                                        }
                                        uint64_t t79 = gm_l13;
                                        uint64_t r80; bool o81 = __builtin_sub_overflow(t79, ((uint64_t)1ULL), &r80);
                                        if (o81) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 36, 13);
                                        uint64_t t82 = r80;
                                        gm_l13 = t82;
                                    }
gm_lb8:;
                                }
                            }
gm_lb6:;
                            void * t83 = (void *)((uint8_t *)&F.l8);
                            uint64_t t84 = gm_l13;
                            uint64_t t85 = gm_l11;
                            gm_tftb1odc739t5 t86 = gm_f_insert_u31m55g(t83, t84, t85);
                            gm_l27 = t86;
                            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src2n2ugogq2cq2, 38, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src2n2ugogq2cq2, 30, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t88 = gm_l10;
                uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                uint64_t t91 = r89;
                gm_l10 = t91;
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t92;
        gm_f_list_uvfu1k1((void *)&t92, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t92;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_l16 = t93;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t94 = gm_l17;
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t96 = (t94 < t95);
                if (!t96) {
                    goto gm_lb9;
                }
                {
                    void * t97 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t99 = gm_l17;
                    {
                        {
                            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t97) + (uintptr_t)t99 * 8));
                            gm_l18 = t100;
                            void * t101 = gm_l1;
                            void * t102 = GM_LD(void *, ((uint8_t *)t101));
                            uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t101) + 8));
                            uint64_t t104 = gm_l18;
                            if ((uintptr_t)t104 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t104, t103, gm_src2n2ugogq2cq2, 42, 15);
                            void * t105 = (void *)(((uint8_t *)t102) + (uintptr_t)t104 * 184);
                            gm_l19 = t105;
                            void * t106 = gm_l19;
                            gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 24));
                            gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            uint8_t t109 = (gm_eqff85ac10o3(((uint8_t *)&t107), ((uint8_t *)&t108)));
                            if (t109) {
                                gm_l28 = ((uint8_t)1);
                            } else {
                                void * t110 = gm_l19;
                                uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)t110) + 24));
                                uint32_t t112 = gm_l2;
                                uint8_t t113 = (!(t111 == t112));
                                gm_l28 = t113;
                            }
                            uint8_t t114 = gm_l28;
                            if (t114) {
                                void * t115 = gm_l19;
                                gm_t9qkh0rsooc8d t116 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t115));
                                uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t116) + 0));
                                uint8_t t118 = (uint8_t)((t117 == ((uint8_t)2ULL)));
                                gm_t9qkh0rsooc8d t119;
                                GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)2ULL));
                                if (t118) {
                                    void * t120 = gm_l0;
                                    void * t121 = gm_l19;
                                    gm_t9qkh0rsooc8d t122 = gm_l5;
                                    void * t123 = (void *)((uint8_t *)&F.l15);
                                    gm_b0_1 t124 = gm_f_render_pi7hvmf((void *)NULL, cx, NULL, t120, t121, t122, t123);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb11;
                            }
                            void * t125 = gm_l19;
                            uint32_t t126 = GM_LD(uint32_t, ((((uint8_t *)t125) + 24) + 4));
                            uint32_t t127 = gm_l3;
                            uint8_t t128 = (t126 >= t127);
                            if (t128) {
                                gm_l30 = ((uint8_t)1);
                            } else {
                                void * t129 = gm_l19;
                                uint32_t t130 = GM_LD(uint32_t, ((((uint8_t *)t129) + 24) + 8));
                                uint32_t t131 = gm_l3;
                                uint8_t t132 = (t130 > t131);
                                gm_l30 = t132;
                            }
                            uint8_t t133 = gm_l30;
                            if (t133) {
                                void * t134 = gm_l19;
                                uint32_t t135 = GM_LD(uint32_t, ((((uint8_t *)t134) + 24) + 4));
                                uint32_t t136 = gm_l6;
                                uint8_t t137 = (t135 <= t136);
                                gm_l29 = t137;
                            } else {
                                gm_l29 = ((uint8_t)0);
                            }
                            uint8_t t138 = gm_l29;
                            gm_l20 = t138;
                            {
                                {
                                    uint8_t t139 = gm_l20;
                                    uint8_t t140 = (uint8_t)!t139;
                                    if (!t140) {
                                        goto gm_lb15;
                                    }
                                    void * t141 = gm_l19;
                                    gm_t9qkh0rsooc8d t142 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t141));
                                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
                                    uint8_t t144 = (uint8_t)(!(t143 == ((uint8_t)2ULL)));
                                    if (!t144) {
                                        goto gm_lb15;
                                    }
                                    goto gm_lb11;
                                }
gm_lb15:;
                            }
gm_lb14:;
                            {
                                gm_trqfvmjtt4sij t145;
                                gm_f_list_rk274m7((void *)&t145, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l21 = t145;
                            }
                            void * t146 = gm_l19;
                            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)t146) + 40));
                            gm_l22 = t147;
                            gm_l23 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t148 = gm_l23;
                                    uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                    uint8_t t150 = (t148 < t149);
                                    if (!t150) {
                                        goto gm_lb16;
                                    }
                                    {
                                        void * t151 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                        uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                        uint64_t t153 = gm_l23;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l24), (((uint8_t *)t151) + (uintptr_t)t153 * 32), 32);
                                                {
                                                    {
                                                        gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l24));
                                                        uint32_t t155 = gm_l2;
                                                        uint32_t t156 = gm_l3;
                                                        uint32_t t157 = gm_l6;
                                                        uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                        uint8_t t159 = gm_l20;
                                                        gm_toceffb697p0u t160 = gm_f_remap_qm0iknp(GM_CONV(gm_cseahvg8v7p0vs, t154), t155, t156, t157, t158, t159);
                                                        gm_l31 = t160;
                                                        uint8_t t161 = (uint8_t)((((uint8_t *)&gm_l31))[12] != 0);
                                                        if (!t161) {
                                                            goto gm_lb22;
                                                        }
                                                        gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l31));
                                                        gm_l25 = t162;
                                                        void * t163 = (void *)((uint8_t *)&F.l21);
                                                        gm_tl874t2atnlh3 t164 = gm_l25;
                                                        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                                                        gm_tbgd14esf0jvi t166;
                                                        GM_ST(uint32_t, (uint8_t *)&t166 + 12, 0);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t166 + 0), t164);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t166 + 16), t165);
                                                        gm_tftb1odc739t5 t167 = gm_f_push_kiclmcn(t163, &t166);
                                                        gm_l32 = t167;
                                                        uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                        if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src2n2ugogq2cq2, 51, 70);
                                                        goto gm_lb21;
                                                    }
gm_lb22:;
                                                }
gm_lb21:;
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                            gm_trap("no match arm matched the scrutinee", gm_src2n2ugogq2cq2, 50, 9);
                                        }
gm_lb19:;
                                    }
gm_lb18:;
                                    uint64_t t169 = gm_l23;
                                    uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                                    uint64_t t172 = r170;
                                    gm_l23 = t172;
                                }
                            }
gm_lb16:;
                            {
                                void * t173 = gm_l19;
                                gm_t9qkh0rsooc8d t174 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t173));
                                void * t175 = gm_l19;
                                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 8));
                                void * t177 = gm_l19;
                                gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t177) + 24));
                                uint32_t t179 = gm_l2;
                                uint32_t t180 = gm_l3;
                                uint32_t t181 = gm_l6;
                                uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint8_t t183 = gm_l20;
                                gm_toceffb697p0u t184 = gm_f_remap_qm0iknp(GM_CONV(gm_cseahvg8v7p0vs, t178), t179, t180, t181, t182, t183);
                                gm_l34 = t184;
                                uint8_t t185 = (uint8_t)((((uint8_t *)&gm_l34))[12] != 0);
                                if (t185) {
                                    gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l34));
                                    gm_l33 = t186;
                                } else {
                                    gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                    gm_l33 = t187;
                                }
                                gm_tl874t2atnlh3 t188 = gm_l33;
                                void * t189 = gm_l19;
                                void * t190 = gm_l19;
                                void * t191 = gm_l19;
                                gm_trqfvmjtt4sij t192 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l21));
                                gm_trqfvmjtt4sij t193 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t189) + 80));
                                gm_trqfvmjtt4sij t194 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t190) + 120));
                                gm_th2lqbo77pvsc t195 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t191) + 160));
                                gm_t4gut2c9fj1kt t196;
                                GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t196 + 36, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t196 + 0), t174);
                                GM_ST(gm_b16_8, ((uint8_t *)&t196 + 8), t176);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t196 + 24), t188);
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t196 + 40), t192);
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t196 + 80), t193);
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t196 + 120), t194);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t196 + 160), t195);
                                F.l26 = t196;
                            }
                            void * t197 = (void *)((uint8_t *)&F.l7);
                            void * t198 = (void *)((uint8_t *)&F.l26);
                            gm_t9qkh0rsooc8d t199 = gm_l5;
                            void * t200 = (void *)((uint8_t *)&F.l15);
                            gm_b0_1 t201 = gm_f_render_pi7hvmf((void *)NULL, cx, NULL, t197, t198, t199, t200);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_src2n2ugogq2cq2, 41, 5);
                    }
gm_lb12:;
                }
gm_lb11:;
                uint64_t t202 = gm_l17;
                uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                uint64_t t205 = r203;
                gm_l17 = t205;
            }
        }
gm_lb9:;
    }
    {
        gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
        F.ret = t207;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_remap_qm0iknp(gm_cseahvg8v7p0vs a0, uint32_t a1, uint32_t a2, uint32_t a3, uint64_t a4, uint8_t a5) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remap_qm0iknp F;
    gm_tl874t2atnlh3 gm_l0; /* s */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    uint64_t gm_l4; /* input_len */
    uint8_t gm_l5; /* inside */
    uint32_t gm_l6; /* a */
    uint32_t gm_l7; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l8;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
        uint32_t t6 = gm_l1;
        uint8_t t7 = (!(t5 == t6));
        gm_l8 = t7;
    }
    uint8_t t8 = gm_l8;
    if (t8) {
        gm_toceffb697p0u t9;
        memset(&t9, 0, sizeof t9);
        F.ret = t9;
        goto gm_fret;
    }
    uint8_t t10 = gm_l5;
    if (t10) {
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint32_t t12 = gm_l2;
        uint32_t t13 = (t11 > t12 ? t11 : t12);
        gm_l6 = t13;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        uint32_t t15 = gm_l3;
        uint32_t r16; bool o17 = __builtin_add_overflow(t15, ((uint32_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_src2n2ugogq2cq2, 72, 23);
        uint32_t t18 = r16;
        uint32_t t19 = (t14 < t18 ? t14 : t18);
        uint32_t t20 = gm_l6;
        uint32_t t21 = (t19 > t20 ? t19 : t20);
        gm_l7 = t21;
        uint32_t t22 = gm_l6;
        uint32_t t23 = gm_l2;
        uint32_t r24; bool o25 = __builtin_sub_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 73, 41);
        uint32_t t26 = r24;
        uint32_t t27 = gm_l7;
        uint32_t t28 = gm_l2;
        uint32_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 73, 54);
        uint32_t t31 = r29;
        uint64_t t32 = gm_l4;
        uint32_t t33 = (uint32_t)t32;
        uint32_t t34 = (t31 < t33 ? t31 : t33);
        gm_tl874t2atnlh3 t35;
        GM_ST(uint32_t, ((uint8_t *)&t35 + 0), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t35 + 4), t26);
        GM_ST(uint32_t, ((uint8_t *)&t35 + 8), t34);
        gm_toceffb697p0u t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t36), t35);
        ((uint8_t *)&t36)[12] = 1;
        F.ret = t36;
        goto gm_fret;
    }
    uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    gm_tl874t2atnlh3 t39;
    GM_ST(uint32_t, ((uint8_t *)&t39 + 0), ((uint32_t)1ULL));
    GM_ST(uint32_t, ((uint8_t *)&t39 + 4), t37);
    GM_ST(uint32_t, ((uint8_t *)&t39 + 8), t38);
    gm_toceffb697p0u t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t40), t39);
    ((uint8_t *)&t40)[12] = 1;
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5) {
    struct gm_fr_render_region_ppot3lm F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    uint32_t gm_l2; /* file */
    uint32_t gm_l3; /* lo */
    gm_b16_8 gm_l4; /* input */
    gm_t9qkh0rsooc8d gm_l5; /* format */
    gm_b16_8 gm_l7; /* text */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    {
        {
            gm_tjt4ka19str13 t5;
            gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l6 = t5;
        }
        {
            void * t6 = (void *)((uint8_t *)&F.l6);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            void * t12 = gm_l0;
            void * t13 = gm_l1;
            uint32_t t14 = gm_l2;
            uint32_t t15 = gm_l3;
            gm_b16_8 t16 = gm_l4;
            gm_t9qkh0rsooc8d t17 = gm_l5;
            gm_b16_8 t18 = gm_f_region_text_snb7ngp((void *)NULL, (&wn9), NULL, t12, t13, t14, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_l8 = t18;
        }
        goto gm_ls19;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls19:;
gm_lb2:;
        F.w0 = gm_l8;
we10:;
        gm_l8 = F.w0;
        gm_b16_8 t20 = gm_l8;
        gm_l7 = t20;
        {
            gm_b16_8 t21 = gm_l7;
            gm_f_dupe_g9iqpvn((void *)&gm_l9, (void *)NULL, cx, NULL, t21, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kold22d1smp)));
            if (gm_unw.kind) {
                goto gm_ld4_1;
            }
        }
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src2n2ugogq2cq2, 20, 12);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        F.ret = t24;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld4_1;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l6);
        gm_f_deinit_sejdpqv(t25);
    }
    goto gm_ls26;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t27 = (void *)((uint8_t *)&F.l6);
        gm_f_deinit_sejdpqv(t27);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls26:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_trap_report_a7hiu65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_trap_report_a7hiu65 F;
    void * gm_l0; /* c */
    void * gm_l1; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* f */
    gm_tcc1rq4n1avb2 gm_l5; /* at */
    gm_tcc1rq4n1avb2 gm_l6; /* start */
    uint32_t gm_l7; /* col */
    GM_STACK_CHECK(&F, 625u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_trqfvmjtt4sij t4;
    gm_f_list_uvfu1k1((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t4;
    void * t5 = (void *)((uint8_t *)&F.l2);
    void * t6 = gm_l1;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t6));
    gm_tnpifv20irnph t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
    gm_tftb1odc739t5 t9 = gm_f_write_rcan3t8(t5, t8);
    gm_l8 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src2n2ugogq2cq2, 81, 5);
    void * t11 = gm_l1;
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 16));
    gm_l3 = t12;
    gm_tl874t2atnlh3 t13 = gm_l3;
    gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t15 = (gm_eqff85ac10o3(((uint8_t *)&t13), ((uint8_t *)&t14)));
    if (t15) {
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        F.ret = t17;
        goto gm_fret;
    }
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    void * t21 = gm_f_file_o15fqku(t19, t20);
    gm_l4 = t21;
    void * t22 = gm_l4;
    uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    gm_tcc1rq4n1avb2 t24 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t22, t23));
    gm_l5 = t24;
    {
        {
            uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            void * t26 = gm_l0;
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 36));
            uint8_t t28 = ((t25 == t27));
            if (!t28) {
                goto gm_lb2;
            }
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            void * t30 = gm_l0;
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 40));
            uint8_t t32 = (t29 >= t31);
            if (!t32) {
                goto gm_lb2;
            }
            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            void * t34 = gm_l0;
            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)t34) + 40));
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 48) + 8));
            uint32_t t38 = (uint32_t)t37;
            uint32_t r39; bool o40 = __builtin_add_overflow(t35, t38, &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_src2n2ugogq2cq2, 86, 63);
            uint32_t t41 = r39;
            uint8_t t42 = (t33 <= t41);
            if (!t42) {
                goto gm_lb2;
            }
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 8));
            void * t45 = gm_l0;
            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)t45) + 36));
            void * t47 = gm_f_file_o15fqku(t44, t46);
            void * t48 = gm_l0;
            uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t48) + 40));
            gm_tcc1rq4n1avb2 t50 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t47, t49));
            gm_l6 = t50;
            uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            uint8_t t53 = ((t51 == t52));
            if (t53) {
                uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                uint32_t r55; bool o56 = __builtin_add_overflow(t54, ((uint32_t)1ULL), &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_src2n2ugogq2cq2, 88, 43);
                uint32_t t57 = r55;
                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                uint32_t r59; bool o60 = __builtin_sub_overflow(t57, t58, &r59);
                if (o60) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 88, 43);
                uint32_t t61 = r59;
                gm_l7 = t61;
            } else {
                uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                gm_l7 = t62;
            }
            void * t63 = (void *)((uint8_t *)&F.l2);
            uint32_t t64 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            uint32_t r66; bool o67 = __builtin_sub_overflow(t64, t65, &r66);
            if (o67) gm_trap("integer overflow in `-`", gm_src2n2ugogq2cq2, 89, 42);
            uint32_t t68 = r66;
            uint32_t r69; bool o70 = __builtin_add_overflow(t68, ((uint32_t)1ULL), &r69);
            if (o70) gm_trap("integer overflow in `+`", gm_src2n2ugogq2cq2, 89, 42);
            uint32_t t71 = r69;
            uint32_t t72 = gm_l7;
            gm_tcc1rq4n1avb2 t73;
            GM_ST(uint32_t, ((uint8_t *)&t73 + 0), t71);
            GM_ST(uint32_t, ((uint8_t *)&t73 + 4), t72);
            gm_tftb1odc739t5 t74 = gm_f_write_diqdb80(t63, t73);
            gm_l9 = t74;
            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src2n2ugogq2cq2, 89, 9);
            goto gm_lb1;
        }
gm_lb2:;
        uint32_t t76 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        void * t77 = gm_l0;
        uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)t77) + 36));
        uint8_t t79 = (!(t76 == t78));
        if (t79) {
            void * t80 = (void *)((uint8_t *)&F.l2);
            void * t81 = gm_l4;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)t81));
            uint32_t t83 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            gm_t5hdnnftkup6m t85;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t82);
            GM_ST(uint32_t, ((uint8_t *)&t85 + 16), t83);
            GM_ST(uint32_t, ((uint8_t *)&t85 + 20), t84);
            gm_tftb1odc739t5 t86 = gm_f_write_a8h402l(t80, &t85);
            gm_l10 = t86;
            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src2n2ugogq2cq2, 91, 9);
        }
    }
gm_lb1:;
    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
    F.ret = t89;
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

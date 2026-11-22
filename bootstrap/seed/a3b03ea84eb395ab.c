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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { uint64_t f0; gm_b16_8 f1; uint16_t f2; uint8_t pe[6]; } gm_t3568dh2osse0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_tbfdi36rjh0el;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint32_t f3; } gm_thrm96fhhdpt5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
_Static_assert(sizeof(gm_t3568dh2osse0) == 32 && _Alignof(gm_t3568dh2osse0) == 8 && offsetof(gm_t3568dh2osse0, f0) == 0 && offsetof(gm_t3568dh2osse0, f1) == 8 && offsetof(gm_t3568dh2osse0, f2) == 24, "layout of `(u64, string, u16)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbfdi36rjh0el) == 40 && _Alignof(gm_tbfdi36rjh0el) == 8 && offsetof(gm_tbfdi36rjh0el, f0) == 0 && offsetof(gm_tbfdi36rjh0el, f1) == 8 && offsetof(gm_tbfdi36rjh0el, f2) == 16 && offsetof(gm_tbfdi36rjh0el, f3) == 24, "layout of `(u64, u64, u64, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_thrm96fhhdpt5) == 40 && _Alignof(gm_thrm96fhhdpt5) == 8 && offsetof(gm_thrm96fhhdpt5, f0) == 0 && offsetof(gm_thrm96fhhdpt5, f1) == 16 && offsetof(gm_thrm96fhhdpt5, f2) == 32 && offsetof(gm_thrm96fhhdpt5, f3) == 36, "layout of `(string, string, u32, u32)`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
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
struct gm_fr_fn_4og9d7k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j072dij {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lsit7cv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_meres03 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_upjjs8g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_c0gif0c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c0h6o29 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c0n18m1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c19sabv {
    gm_frame h;
    gm_tbfdi36rjh0el l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c1s3684 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c243onv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2ann39 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2lphrf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2otgb4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c31q7f8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c3isg16 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c57v5iu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c5ago2c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c5b6tc3 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c610h7d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c6vg145 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c726p1u {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8e400b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8fgv1v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8gbrg9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8qq9n9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c97t5s6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c9b4vl3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ccpi8au {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ce9c14n {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cebtc0e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cf0fv9e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cgo7upp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cgpq8i6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ch41dev {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_chnm9ud {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ci5hb2n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cibac8a {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_civj829 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cjdn0sp {
    gm_frame h;
    gm_thognv9odov03 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cjnp8ds {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ck7vger {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ckgfhm4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ckll4kg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_clarrev {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cn0m7al {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cn4968h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cncadt9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cnhv2id {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_convcvp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cp0hre9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cp3hiv2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpa0si0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpfjqa5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpioo77 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqh7lia {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqr3tc0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqv4884 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cub3u7l {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cuuc21t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cv7qh05 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cvqjb45 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s1gurul {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s1oodgm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s20j3pl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s2bvqvf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s2ml2tu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s44fp1q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s4f2nfi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s4jotpe {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s4n90cc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s51nisc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s56aern {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s7bl5bp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s7ci5ul {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8ajp67 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8juhic {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8qs6vn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s9gvae7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sa1940l {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sbfc2dl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sbobj14 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sc59up9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sdje4ev {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_seuq78o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sflh0jm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sfq90nb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sgnhhmi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_shk2dul {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_shkdld2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sirnlvi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sl0efd0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_slm5ovr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_slub3h9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_snpkhcp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_soc2hp6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sojmgl9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sphfl1m {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_spi49tf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sq1gio6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sqbp092 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sscvcf2 {
    gm_frame h;
    gm_thrm96fhhdpt5 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_stg33ch {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_stmnn5h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sugh2p3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sv91363 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_svnhg71 {
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4og9d7k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j072dij(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lsit7cv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meres03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upjjs8g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_0g25qf5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_35467gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_4ggj4mf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_4l7juhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c0gif0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_c0n18m1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c1s3684(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c243onv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
inline __attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c610h7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c6vg145(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c8e400b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c8gbrg9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cf0fv9e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cgo7upp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_chnm9ud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ci5hb2n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cibac8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_civj829(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ckll4kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_clarrev(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cp3hiv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cpfjqa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cqh7lia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cub3u7l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dq39vq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_flhcmdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_j9cgp0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n2k0uck(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_p453i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_p95juln(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rkil4k4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s4f2nfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s56aern(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sbobj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sdje4ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_sfq90nb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sgnhhmi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_shk2dul(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_shkdld2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sirnlvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_snpkhcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sphfl1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_spi49tf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sscvcf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_stg33ch(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_te7kh3c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s13jru0todls1[38];
extern const uint8_t gm_s1fcd4t8vd2g5[30];
extern const uint8_t gm_s1mgkad218qag[19];
extern const uint8_t gm_s2nj060fs0sf4[6];
extern const uint8_t gm_s2to760maglj8[13];
extern const uint8_t gm_s31o876d6qij5[10];
extern const uint8_t gm_s3jpgh4kdh4qh[15];
extern const uint8_t gm_s3kin7bltuqdc[35];
extern const uint8_t gm_s3s5u5e6aukj7[48];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s58b2tdsrfkg1[85];
extern const uint8_t gm_s5bvhb1j14g3q[9];
extern const uint8_t gm_s5m5egkp3rutu[118];
extern const uint8_t gm_s5rb1e1n8ivr7[48];
extern const uint8_t gm_s65ir5du2gd8b[9];
extern const uint8_t gm_s6i2dst792b3t[54];
extern const uint8_t gm_s6rivmf4qclvb[71];
extern const uint8_t gm_s70g70juoj4nh[42];
extern const uint8_t gm_s7ulscihp75vl[73];
extern const uint8_t gm_s86g8amsftemj[8];
extern const uint8_t gm_s8jc0a1p1kvtf[73];
extern const uint8_t gm_s95gukpblkpua[33];
extern const uint8_t gm_s9k2jkfctp1g5[72];
extern const uint8_t gm_s9nm0mpn8r8ov[40];
extern const uint8_t gm_s9po3krkcue72[4];
extern const uint8_t gm_s9sakn2pk9s4e[19];
extern const uint8_t gm_sa64nb2j51s60[19];
extern const uint8_t gm_sajo3kdi51jpa[10];
extern const uint8_t gm_savs0ep448c40[33];
extern const uint8_t gm_sb0j8pu1hhl5q[52];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb5h01ig0b9oe[34];
extern const uint8_t gm_sb7kik80lfbge[11];
extern const uint8_t gm_sbfpg6hac36v9[19];
extern const uint8_t gm_sc51jgguf848t[70];
extern const uint8_t gm_sckj2gedlq7u6[55];
extern const uint8_t gm_scqusp7lgvp02[4];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sdstli6hlsa1e[7];
extern const uint8_t gm_se25mtccq2mtg[42];
extern const uint8_t gm_sffbik0ast0lg[53];
extern const uint8_t gm_sffcmqsfdatal[47];
extern const uint8_t gm_sfggcm2ak7679[20];
extern const uint8_t gm_sfp78bn17ei97[60];
extern const uint8_t gm_sg6dqv3rmh13r[59];
extern const uint8_t gm_sgt05f0dtajpv[18];
extern const uint8_t gm_sguqrpr7akp41[28];
extern const uint8_t gm_sh0adpq1lnkpj[40];
extern const uint8_t gm_shjfpng3ei3du[82];
extern const uint8_t gm_shl6ef6if4a86[80];
extern const uint8_t gm_shn9a2s1f1vl0[17];
extern const uint8_t gm_shroj9d6fajb6[4];
extern const uint8_t gm_si5ao9la9pflk[19];
extern const uint8_t gm_siklni1kauva6[6];
extern const uint8_t gm_sj718thiqudru[10];
extern const uint8_t gm_sjijer52q1op6[7];
extern const uint8_t gm_sjjb6u8f31rna[15];
extern const uint8_t gm_sjpml17j24nb4[64];
extern const uint8_t gm_sjsaqpo8dvqo2[82];
extern const uint8_t gm_sl4ll6nrpr5rj[33];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slff7lshtgu6r[42];
extern const uint8_t gm_sllaecjankikg[63];
extern const uint8_t gm_slmprk6mos437[8];
extern const uint8_t gm_sm7oa350rtqnm[12];
extern const uint8_t gm_smk40q3nf1t3o[32];
extern const uint8_t gm_smr2fm6cfetj8[5];
extern const uint8_t gm_smsmo0f5ao530[10];
extern const uint8_t gm_smu1qqop2463k[53];
extern const uint8_t gm_sn0ocj5vv0ci3[114];
extern const uint8_t gm_snlpcspgan3ri[16];
extern const uint8_t gm_so99jjk23jgg8[5];
extern const uint8_t gm_sonq8sf0vvvvr[127];
extern const uint8_t gm_soqpbc95nno1l[7];
extern const uint8_t gm_spbqvmg1179vc[24];
extern const uint8_t gm_spica4gaklfma[27];
extern const uint8_t gm_spvfph5mfb023[16];
extern const uint8_t gm_sqcj2s8m7mpqj[38];
extern const uint8_t gm_sqldj8dr0j7rf[12];
extern const uint8_t gm_sr2oeunjjbqtf[14];
extern const uint8_t gm_sr2ptnspelf8s[98];
extern const uint8_t gm_sr5j1k0g236qh[68];
extern const uint8_t gm_sr5s632lvio16[62];
extern const uint8_t gm_srajaqomjf1d3[19];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srv8c2gciqkuj[48];
extern const uint8_t gm_ss7dlauv50bt8[81];
extern const uint8_t gm_sse4og0vn1mc5[33];
extern const uint8_t gm_sshop8kf5qfhm[63];
extern const uint8_t gm_ssm3bl5los9iv[5];
extern const uint8_t gm_su0agms0tqg5k[15];
extern const uint8_t gm_su8mkvs9pi0v1[18];
extern const uint8_t gm_sup9bfr0np428[9];
extern const uint8_t gm_surrhqnv1nojd[46];
extern const uint8_t gm_sv3d9namfmaua[9];
extern const uint8_t gm_svg70al9ibq8e[56];
extern const uint8_t gm_svgiumrfk8vkf[73];
extern const gm_tmpl gm_tpl00eps1l57c5a;
extern const gm_tmpl gm_tpl0e4rd6ag9tqa;
extern const gm_tmpl gm_tpl0emlhhlpmv0s;
extern const gm_tmpl gm_tpl0eumfheo3vtt;
extern const gm_tmpl gm_tpl13591hp4g6qh;
extern const gm_tmpl gm_tpl1a3usdkusvtq;
extern const gm_tmpl gm_tpl1dirc8ldrsao;
extern const gm_tmpl gm_tpl1rfj3tqlepqo;
extern const gm_tmpl gm_tpl1t0vlt7tq67r;
extern const gm_tmpl gm_tpl2cg0ghfdkfbm;
extern const gm_tmpl gm_tpl2od99j0nh2ah;
extern const gm_tmpl gm_tpl355ags9581ob;
extern const gm_tmpl gm_tpl3lqpj3d2egtb;
extern const gm_tmpl gm_tpl3qt6ss8l9cgf;
extern const gm_tmpl gm_tpl40m4ah8dc3ae;
extern const gm_tmpl gm_tpl4eg9pptom3ij;
extern const gm_tmpl gm_tpl4esqdh06rpvt;
extern const gm_tmpl gm_tpl4g1h0tr3apfi;
extern const gm_tmpl gm_tpl4gs3p00joms4;
extern const gm_tmpl gm_tpl4s6jv60d1ova;
extern const gm_tmpl gm_tpl5677p7i476ao;
extern const gm_tmpl gm_tpl5a8og2hcltt3;
extern const gm_tmpl gm_tpl5b7905cihaqk;
extern const gm_tmpl gm_tpl5cgoa6ddohcv;
extern const gm_tmpl gm_tpl5o8dcpa0ctot;
extern const gm_tmpl gm_tpl5qgrl10qvakk;
extern const gm_tmpl gm_tpl6gc9e2atg845;
extern const gm_tmpl gm_tpl6itong91gv3f;
extern const gm_tmpl gm_tpl6nq0lpkkblpc;
extern const gm_tmpl gm_tpl82mi3mv6t0is;
extern const gm_tmpl gm_tpl84r77qupuujj;
extern const gm_tmpl gm_tpl8g9lchslmbjf;
extern const gm_tmpl gm_tpl9v7lb7n4a602;
extern const gm_tmpl gm_tplaa2bqbq660if;
extern const gm_tmpl gm_tplad88kt6677ij;
extern const gm_tmpl gm_tplao1juqpqjfv9;
extern const gm_tmpl gm_tplbj3gvifc61li;
extern const gm_tmpl gm_tplbo3vrh9egnst;
extern const gm_tmpl gm_tplcaav01djo2md;
extern const gm_tmpl gm_tplctspgtksm35f;
extern const gm_tmpl gm_tplcvnbkkkji94u;
extern const gm_tmpl gm_tpld1hhk0kef14l;
extern const gm_tmpl gm_tpldftt3jdrahlt;
extern const gm_tmpl gm_tpldl4pn21ood35;
extern const gm_tmpl gm_tple41tg2t64d1i;
extern const gm_tmpl gm_tple68gu1adcjq9;
extern const gm_tmpl gm_tple7qj0fr2tlub;
extern const gm_tmpl gm_tpleeuqgcr11ngn;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplepdvp8ofjal7;
extern const gm_tmpl gm_tplfe53mjoakni4;
extern const gm_tmpl gm_tplfm527r729efa;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplg3ifbt1a32e1;
extern const gm_tmpl gm_tplgism5t1v67p6;
extern const gm_tmpl gm_tplhciekfbsqng7;
extern const gm_tmpl gm_tplhi0fd9nctd4b;
extern const gm_tmpl gm_tplhn9hprqs819d;
extern const gm_tmpl gm_tpli0tmonaamd45;
extern const gm_tmpl gm_tpli2qk88n6tll4;
extern const gm_tmpl gm_tpli4rfoi75onhj;
extern const gm_tmpl gm_tpljc7kqi31f4d2;
extern const gm_tmpl gm_tpljtaapes6rh2u;
extern const gm_tmpl gm_tpljueetjft1ril;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tpllf7nufk7lvkm;
extern const gm_tmpl gm_tpllfgb42aufjqd;
extern const gm_tmpl gm_tpllv3q9mfan4rg;
extern const gm_tmpl gm_tplm2m182mt5ljj;
extern const gm_tmpl gm_tplm6g8a9odsasi;
extern const gm_tmpl gm_tplme3pul5hs3bf;
extern const gm_tmpl gm_tplmk3amvcsk2gu;
extern const gm_tmpl gm_tplmnlel5aohei6;
extern const gm_tmpl gm_tplmr9p697ovoo7;
extern const gm_tmpl gm_tpln1ol2o7bp7d9;
extern const gm_tmpl gm_tpln92q8vfbc4aq;
extern const gm_tmpl gm_tplntk567ck43t8;
extern const gm_tmpl gm_tplodcipjau6eqr;
extern const gm_tmpl gm_tploj4uaptg6qnk;
extern const gm_tmpl gm_tplomrgvu31uqof;
extern const gm_tmpl gm_tplp3ak0q2gramc;
extern const gm_tmpl gm_tplptu6sgqf50o0;
extern const gm_tmpl gm_tplqbglsd979t5c;
extern const gm_tmpl gm_tplqnml5avaeikk;
extern const gm_tmpl gm_tplqrah664loq9e;
extern const gm_tmpl gm_tplr35nagqnpmad;
extern const gm_tmpl gm_tplsdamq31ts61q;
extern const gm_tmpl gm_tplshqe0v39oc52;
extern const gm_tmpl gm_tplsqibbtt8789o;
extern const gm_tmpl gm_tplt9d80smn1kum;
extern const gm_tmpl gm_tpltb9h1t5060dp;
extern const gm_tmpl gm_tpltrvmjk8jhmh7;
extern const gm_tmpl gm_tplts1d4nefj5gf;
extern const gm_tmpl gm_tplulq1qidn3nog;
extern const gm_tmpl gm_tplvbcfr0k6nmhv;
extern const gm_tmpl gm_tplvkvnlb2orh55;
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty929lennfd3;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tyqa6gfa8omk;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4og9d7k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4og9d7k F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_cp3hiv2 *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_cp3hiv2 *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_cp3hiv2 *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j072dij(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j072dij F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_s2bvqvf *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_s2bvqvf *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_s2bvqvf *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lsit7cv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lsit7cv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_sc59up9 *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_sc59up9 *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_sc59up9 *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meres03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_meres03 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_c97t5s6 *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_c97t5s6 *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_c97t5s6 *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upjjs8g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_upjjs8g F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_shk2dul *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_shk2dul *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_shk2dul *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_c0gif0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c0gif0c F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_c0h6o29 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scqusp7lgvp02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr35nagqnpmad), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c0n18m1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c0n18m1 F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sguqrpr7akp41));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplts1d4nefj5gf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c19sabv F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8jc0a1p1kvtf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_tbfdi36rjh0el t3 = GM_LD(gm_tbfdi36rjh0el, ((uint8_t *)a0));
    gm_f_format_4ggj4mf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1t0vlt7tq67r), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c1s3684(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c1s3684 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su0agms0tqg5k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5a8og2hcltt3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c243onv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c243onv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slmprk6mos437));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple68gu1adcjq9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_c2ann39 F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb5h01ig0b9oe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4eg9pptom3ij), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c2lphrf F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_dq39vq2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c2otgb4 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snlpcspgan3ri));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplodcipjau6eqr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c31q7f8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2nj060fs0sf4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljtaapes6rh2u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c3isg16 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6i2dst792b3t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleeuqgcr11ngn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c57v5iu F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smu1qqop2463k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(52));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2cg0ghfdkfbm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c5ago2c F;
    gm_t3568dh2osse0 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sshop8kf5qfhm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t3568dh2osse0 t3 = GM_LD(gm_t3568dh2osse0, ((uint8_t *)a0));
    gm_f_format_35467gd((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmr9p697ovoo7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c5b6tc3 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh0adpq1lnkpj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1dirc8ldrsao), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c610h7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c610h7d F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjsaqpo8dvqo2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(81));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4esqdh06rpvt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c6vg145(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c6vg145 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spica4gaklfma));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4gs3p00joms4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c726p1u F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s31o876d6qij5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqnml5avaeikk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c8e400b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c8e400b F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1fcd4t8vd2g5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl00eps1l57c5a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c8fgv1v F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sffcmqsfdatal));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbo3vrh9egnst), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c8gbrg9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c8gbrg9 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s65ir5du2gd8b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1a3usdkusvtq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c8qq9n9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss7dlauv50bt8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm2m182mt5ljj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c9b4vl3 F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3jpgh4kdh4qh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln1ol2o7bp7d9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ccpi8au F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl4ll6nrpr5rj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_flhcmdb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple41tg2t64d1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ce9c14n F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6rivmf4qclvb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(70));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg3ifbt1a32e1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cebtc0e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7ulscihp75vl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplntk567ck43t8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cf0fv9e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cf0fv9e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb0j8pu1hhl5q));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhi0fd9nctd4b), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cgo7upp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cgo7upp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc51jgguf848t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5qgrl10qvakk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cgpq8i6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5rb1e1n8ivr7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt9d80smn1kum), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ch41dev F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sajo3kdi51jpa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4g1h0tr3apfi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_chnm9ud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_chnm9ud F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srv8c2gciqkuj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbj3gvifc61li), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ci5hb2n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ci5hb2n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9k2jkfctp1g5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhn9hprqs819d), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cibac8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cibac8a F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa64nb2j51s60));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6itong91gv3f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_civj829(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_civj829 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sup9bfr0np428));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9v7lb7n4a602), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cjdn0sp F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shn9a2s1f1vl0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(16));
    gm_thognv9odov03 t3 = GM_LD(gm_thognv9odov03, ((uint8_t *)a0));
    gm_f_format_p95juln((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgism5t1v67p6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cjnp8ds F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_savs0ep448c40));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tjftu5su6fh8e t3 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
    gm_f_format_p453i5r((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6gc9e2atg845), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ck7vger F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s58b2tdsrfkg1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(84));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltb9h1t5060dp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ckgfhm4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqcj2s8m7mpqj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld1hhk0kef14l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ckll4kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ckll4kg F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqldj8dr0j7rf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljueetjft1ril), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_clarrev(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_clarrev F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si5ao9la9pflk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_0g25qf5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplulq1qidn3nog), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cn0m7al F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfp78bn17ei97));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqrah664loq9e), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cn4968h F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv3d9namfmaua));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3qt6ss8l9cgf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cncadt9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssm3bl5los9iv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplepdvp8ofjal7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cnhv2id F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svgiumrfk8vkf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllf7nufk7lvkm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_convcvp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjpml17j24nb4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6nq0lpkkblpc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cp0hre9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgt05f0dtajpv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplptu6sgqf50o0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cpa0si0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srajaqomjf1d3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldl4pn21ood35), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cpfjqa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cpfjqa5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdstli6hlsa1e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl3lqpj3d2egtb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cpioo77 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr5j1k0g236qh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl355ags9581ob), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cqh7lia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqh7lia F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3s5u5e6aukj7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl13591hp4g6qh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqr3tc0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjijer52q1op6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltrvmjk8jhmh7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqv4884 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svg70al9ibq8e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl84r77qupuujj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cub3u7l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cub3u7l F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr5s632lvio16));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhciekfbsqng7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cuuc21t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfggcm2ak7679));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvbcfr0k6nmhv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cv7qh05 F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb7kik80lfbge));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_s2bvqvf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvkvnlb2orh55), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cvqjb45 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn0ocj5vv0ci3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(113));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0emlhhlpmv0s), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s1gurul F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sonq8sf0vvvvr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(126));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0e4rd6ag9tqa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s1oodgm F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr2ptnspelf8s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(97));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsqibbtt8789o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s20j3pl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soqpbc95nno1l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljc7kqi31f4d2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s2ml2tu F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_se25mtccq2mtg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_4l7juhh((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl82mi3mv6t0is), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s44fp1q F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5bvhb1j14g3q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmnlel5aohei6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s4f2nfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s4f2nfi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siklni1kauva6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplctspgtksm35f), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s4jotpe F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj718thiqudru));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8g9lchslmbjf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_s4n90cc F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd4qrpv3lpilp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli0tmonaamd45), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s51nisc F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr2oeunjjbqtf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplad88kt6677ij), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s56aern(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s56aern F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s13jru0todls1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5677p7i476ao), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s7bl5bp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shjfpng3ei3du));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllv3q9mfan4rg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s7ci5ul F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95gukpblkpua));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploj4uaptg6qnk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s8ajp67 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3kin7bltuqdc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllfgb42aufjqd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s8juhic F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smk40q3nf1t3o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqbglsd979t5c), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s8qs6vn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shl6ef6if4a86));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(79));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5b7905cihaqk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s9gvae7 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg6dqv3rmh13r));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_cp3hiv2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplaa2bqbq660if), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_sa1940l F;
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
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sbfc2dl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8mkvs9pi0v1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli2qk88n6tll4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sbobj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_sbobj14 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9sakn2pk9s4e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5cgoa6ddohcv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sdje4ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_sdje4ev F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9po3krkcue72));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfm527r729efa), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_seuq78o F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sffbik0ast0lg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplao1juqpqjfv9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_sflh0jm F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sckj2gedlq7u6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli4rfoi75onhj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sfq90nb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sfq90nb F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spbqvmg1179vc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4s6jv60d1ova), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sgnhhmi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sgnhhmi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5m5egkp3rutu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(117));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmk3amvcsk2gu), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_shkdld2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_shkdld2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1mgkad218qag));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcaav01djo2md), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sirnlvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sirnlvi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjjb6u8f31rna));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2od99j0nh2ah), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sl0efd0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sllaecjankikg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1rfj3tqlepqo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_slm5ovr F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slff7lshtgu6r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfe53mjoakni4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_slub3h9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s70g70juoj4nh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl40m4ah8dc3ae), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_snpkhcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_snpkhcp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spvfph5mfb023));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldftt3jdrahlt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_soc2hp6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so99jjk23jgg8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0eumfheo3vtt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sojmgl9 F;
    gm_tvaas22dl64e0 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_tvaas22dl64e0 t3 = GM_LD(gm_tvaas22dl64e0, ((uint8_t *)a0));
    gm_f_format_te7kh3c((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sphfl1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sphfl1m F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_surrhqnv1nojd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(45));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcvnbkkkji94u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_spi49tf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_spi49tf F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shroj9d6fajb6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplshqe0v39oc52), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sq1gio6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nm0mpn8r8ov));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsdamq31ts61q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sqbp092 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86g8amsftemj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplme3pul5hs3bf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sscvcf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sscvcf2 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sse4og0vn1mc5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_thrm96fhhdpt5 t3 = GM_LD(gm_thrm96fhhdpt5, ((uint8_t *)a0));
    gm_f_format_n2k0uck((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplp3ak0q2gramc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_stg33ch(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_stg33ch F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smr2fm6cfetj8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplomrgvu31uqof), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_stmnn5h F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm7oa350rtqnm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5o8dcpa0ctot), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sugh2p3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smsmo0f5ao530));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm6g8a9odsasi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sv91363 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbfpg6hac36v9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln92q8vfbc4aq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_svnhg71 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2to760maglj8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple7qj0fr2tlub), t3);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4og9d7k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j072dij(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lsit7cv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meres03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upjjs8g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) void gm_f_format_c0gif0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_c0n18m1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c1s3684(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c243onv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
extern __attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c610h7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c6vg145(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_c8e400b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c8gbrg9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_c97t5s6 F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
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
    gm_t9qkh0rsooc8d t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_meres03, (void *)&F.h, cx});
    void * t13 = gm_l4;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
    const gm_tmpl *tp15 = (const gm_tmpl *)(t13);
    const void *vp15[] = {(const void *)(((uint8_t *)&t14))};
    const gm_type *vd15[] = {&gm_ty929lennfd3};
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
extern __attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cf0fv9e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cgo7upp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_chnm9ud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ci5hb2n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cibac8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_civj829(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ckll4kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_clarrev(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cp3hiv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_cp3hiv2 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    gm_tbccvikg6b96e gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 264u);
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
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
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
        memmove(((uint8_t *)&gm_l3), ((uint8_t *)a0), 32);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_4og9d7k, (void *)&F.h, cx});
    void * t12 = gm_l4;
    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
    const gm_tmpl *tp16 = (const gm_tmpl *)(t12);
    const void *vp16[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15))};
    const gm_type *vd16[] = {&gm_tyb2nno7n67r, &gm_tyb2nno7n67r, &gm_ty0iqiubfm7e};
    static const uint8_t vs16[] = {0, 0, 1};
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
extern __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cpfjqa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cqh7lia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cub3u7l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_s2bvqvf F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
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
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_j072dij, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 20));
    const gm_tmpl *tp16 = (const gm_tmpl *)(t12);
    const void *vp16[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15))};
    const gm_type *vd16[] = {&gm_ty0iqiubfm7e, &gm_ty467d5cm1d5, &gm_ty467d5cm1d5};
    static const uint8_t vs16[] = {1, 0, 0};
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
extern __attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s4f2nfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s56aern(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sbobj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_sc59up9 F;
    gm_t19enj90vg8bd gm_l0; /* args */
    gm_t19enj90vg8bd gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 184u);
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
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
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
    gm_t19enj90vg8bd t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_lsit7cv, (void *)&F.h, cx});
    void * t13 = gm_l4;
    int32_t t14 = GM_LD(int32_t, ((uint8_t *)&gm_l3));
    const gm_tmpl *tp15 = (const gm_tmpl *)(t13);
    const void *vp15[] = {(const void *)(((uint8_t *)&t14))};
    const gm_type *vd15[] = {&gm_tyqa6gfa8omk};
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
extern __attribute__((aligned(4))) void gm_f_format_sdje4ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
extern __attribute__((aligned(4))) void gm_f_format_sfq90nb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_sgnhhmi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_shk2dul(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_shk2dul F;
    gm_toh0ootodhjue gm_l0; /* args */
    gm_toh0ootodhjue gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 180u);
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
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)3ULL), &r5);
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
    gm_toh0ootodhjue t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_upjjs8g, (void *)&F.h, cx});
    void * t13 = gm_l4;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 1));
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 2));
    const gm_tmpl *tp17 = (const gm_tmpl *)(t13);
    const void *vp17[] = {(const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16))};
    const gm_type *vd17[] = {&gm_ty929lennfd3, &gm_ty929lennfd3, &gm_ty929lennfd3};
    static const uint8_t vs17[] = {0, 0, 0};
    gm_closure t18 = t12;
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
extern __attribute__((aligned(4))) void gm_f_format_shkdld2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sirnlvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_snpkhcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_sphfl1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_spi49tf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sscvcf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_stg33ch(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);

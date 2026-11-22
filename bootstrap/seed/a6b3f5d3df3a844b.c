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
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[1]; } gm_tivanu2mfcv6o;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; } gm_t2s5j3n0guq0t;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tivanu2mfcv6o) == 1 && _Alignof(gm_tivanu2mfcv6o) == 1 && offsetof(gm_tivanu2mfcv6o, a) == 0, "layout of `[1]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { void * f0; void * f1; void * f2; void * f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; gm_b16_8 f7; } gm_toqvmi7jqorid;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_th2lqbo77pvsc f1; uint64_t f2; } gm_t04fuf8kbk07t;
_Static_assert(sizeof(gm_t2s5j3n0guq0t) == 24 && _Alignof(gm_t2s5j3n0guq0t) == 8 && offsetof(gm_t2s5j3n0guq0t, f0) == 0 && offsetof(gm_t2s5j3n0guq0t, f1) == 4 && offsetof(gm_t2s5j3n0guq0t, f2) == 8, "layout of `(u32, u32, string)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_toqvmi7jqorid) == 64 && _Alignof(gm_toqvmi7jqorid) == 8 && offsetof(gm_toqvmi7jqorid, f0) == 0 && offsetof(gm_toqvmi7jqorid, f1) == 8 && offsetof(gm_toqvmi7jqorid, f2) == 16 && offsetof(gm_toqvmi7jqorid, f3) == 24 && offsetof(gm_toqvmi7jqorid, f4) == 32 && offsetof(gm_toqvmi7jqorid, f5) == 36 && offsetof(gm_toqvmi7jqorid, f6) == 40 && offsetof(gm_toqvmi7jqorid, f7) == 48, "layout of `Compiled`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; } gm_tvcg39brotose;
_Static_assert(sizeof(gm_t04fuf8kbk07t) == 40 && _Alignof(gm_t04fuf8kbk07t) == 8 && offsetof(gm_t04fuf8kbk07t, f0) == 0 && offsetof(gm_t04fuf8kbk07t, f1) == 8 && offsetof(gm_t04fuf8kbk07t, f2) == 32, "layout of `Hit`");
typedef struct { gm_th2lqbo77pvsc f0; gm_ti5ramhglnvdt f1; uint8_t f2; uint8_t pe[7]; } gm_t9feugf5hcdtg;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Tail`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tvcg39brotose v0; gm_b16_8 v1; } f1; } gm_tdpp5715iet0a;
typedef struct { gm_tvcg39brotose f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; uint32_t f4; uint8_t pe[4]; } gm_tec68k60rd12v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t04fuf8kbk07t v0; gm_b16_8 v1; } f1; } gm_tepv5jklv667k;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tvcg39brotose) == 48 && _Alignof(gm_tvcg39brotose) == 8 && offsetof(gm_tvcg39brotose, f0) == 0 && offsetof(gm_tvcg39brotose, f1) == 1 && offsetof(gm_tvcg39brotose, f2) == 2 && offsetof(gm_tvcg39brotose, f3) == 8 && offsetof(gm_tvcg39brotose, f4) == 32, "layout of `Options`");
_Static_assert(sizeof(gm_t9feugf5hcdtg) == 64 && _Alignof(gm_t9feugf5hcdtg) == 8 && offsetof(gm_t9feugf5hcdtg, f0) == 0 && offsetof(gm_t9feugf5hcdtg, f1) == 24 && offsetof(gm_t9feugf5hcdtg, f2) == 56, "layout of `Classified`");
_Static_assert(sizeof(gm_tdpp5715iet0a) == 56 && _Alignof(gm_tdpp5715iet0a) == 8 && offsetof(gm_tdpp5715iet0a, f0) == 0 && offsetof(gm_tdpp5715iet0a, f1) == 8, "layout of `Cli`");
_Static_assert(sizeof(gm_tec68k60rd12v) == 176 && _Alignof(gm_tec68k60rd12v) == 8 && offsetof(gm_tec68k60rd12v, f0) == 0 && offsetof(gm_tec68k60rd12v, f1) == 48 && offsetof(gm_tec68k60rd12v, f2) == 88 && offsetof(gm_tec68k60rd12v, f3) == 128 && offsetof(gm_tec68k60rd12v, f4) == 168, "layout of `Repl`");
_Static_assert(sizeof(gm_tepv5jklv667k) == 48 && _Alignof(gm_tepv5jklv667k) == 8 && offsetof(gm_tepv5jklv667k, f0) == 0 && offsetof(gm_tepv5jklv667k, f1) == 8, "layout of `Scan`");
struct gm_fr_clear_3ibm3tg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_gp35vgp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_h4369ui {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_sl9fc3u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_command_7gn4uju {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_command_run_06r7r92 {
    gm_frame h;
    gm_tvcg39brotose l0; /* options */
    gm_tvcg39brotose l1; /* opts */
    gm_tec68k60rd12v l4; /* r */
    gm_trqfvmjtt4sij l6; /* pending */
    gm_t9feugf5hcdtg l11; /* c */
    uint8_t ret;
};
struct gm_fr_compile_rj09gp7 {
    gm_frame h;
    gm_td6svc7gs468a l4; /* tail */
    gm_trqfvmjtt4sij l7; /* src */
    gm_tfeab279c16rr l24; /* t */
    gm_toqvmi7jqorid l25; /* c */
    gm_toqvmi7jqorid ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_deinit_gvn47sj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_51k8cgl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_icu3qs9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q5bjns8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_e1j40m3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_5a05gmg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0ggm04 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uq5dnfv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_space_pq8dr45 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_1t36jvm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_93qogsr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_glg30gi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_session_6f2qqqv {
    gm_frame h;
    gm_tk9n3vlila9qg l6; /* rec */
    gm_b0_1 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_file_3kdl78m {
    gm_frame h;
    gm_t9feugf5hcdtg l6; /* c */
    gm_trqfvmjtt4sij l8; /* rest */
    gm_tk9n3vlila9qg l12; /* i */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_tk9n3vlila9qg l21; /* i */
    uint8_t ret;
};
struct gm_fr_new_repl_hdsg2o4 {
    gm_frame h;
    gm_tvcg39brotose l0; /* opts */
    gm_tec68k60rd12v ret;
};
struct gm_fr_parse_options_9st61k7 {
    gm_frame h;
    gm_tvcg39brotose l1; /* o */
    gm_t04fuf8kbk07t l4; /* hit */
    gm_tdpp5715iet0a ret;
};
struct gm_fr_push_1e500nc_2 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_52bt1f1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ej78v89 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f9bu01m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t6e85n8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_error_text_1lgi2bl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_read_line_1oksar8 {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* bytes */
    gm_tl572d7579hns ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_967kt0a_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9pv3nuf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ko1se61 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_pcenrov {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_mdh37fq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_renamed_vars_fho5hvj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_replay_texts_3te9pi7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_tk9n3vlila9qg l5; /* rec */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_reset_pb2jqrj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_var_pjsp5eq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_861qdlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_shown_o0bbr93 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_statements_dafpns9 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* defs */
    gm_trqfvmjtt4sij l4; /* next_vars */
    gm_trqfvmjtt4sij l9; /* uses */
    gm_trqfvmjtt4sij l15; /* replay */
    gm_tfeab279c16rr l16; /* raw */
    gm_toqvmi7jqorid l17; /* c_raw */
    gm_toqvmi7jqorid l18; /* chosen */
    gm_tfeab279c16rr l20; /* tail */
    gm_toqvmi7jqorid l21; /* c_val */
    gm_trqfvmjtt4sij l28; /* pure */
    gm_tfeab279c16rr l29; /* tail */
    gm_toqvmi7jqorid l30; /* probe */
    gm_trqfvmjtt4sij l33; /* names */
    uint8_t ret;
};
struct gm_fr_submit_pah0u77 {
    gm_frame h;
    gm_t9feugf5hcdtg l2; /* c */
    gm_tk9n3vlila9qg l9; /* i */
    gm_b0_1 ret;
};
struct gm_fr_submit_text_7dvfev6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_newlines_9rj3i73 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_try_decls_23vcohd {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* names */
    gm_tk9n3vlila9qg l6; /* i */
    gm_trqfvmjtt4sij l10; /* replay */
    gm_toqvmi7jqorid l11; /* c */
    gm_trqfvmjtt4sij l12; /* kept */
    gm_tk9n3vlila9qg l25; /* i */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_type_of_b1a5sco {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* replay */
    gm_trqfvmjtt4sij l3; /* uses */
    gm_tfeab279c16rr l5; /* direct */
    gm_toqvmi7jqorid l6; /* c */
    gm_tfeab279c16rr l7; /* bound */
    gm_toqvmi7jqorid l8; /* c2 */
    gm_b0_1 ret;
};
struct gm_fr_write_0gtanrr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_nre2qb9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_check_source_nh9qcnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_clear_3ibm3tg(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_gp35vgp(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_sl9fc3u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_51k8cgl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q5bjns8(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_uq5dnfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_1t36jvm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_93qogsr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_glg30gi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_52bt1f1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f9bu01m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6e85n8_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9pv3nuf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ko1se61(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_mdh37fq(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
__attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_run_aqa5hss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_scan_ll1gqkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_shown_o0bbr93(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0gtanrr(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nre2qb9(void * a0, const void * a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern int32_t gm_x_isatty_dhpcvep(int32_t) __asm__(GM_SYM("isatty"));
extern uint8_t gm_c5dv10uirua[16] __attribute__((aligned(8)));
extern uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
extern uint8_t gm_cd231ebi1kg[16] __attribute__((aligned(8)));
extern uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
extern uint8_t gm_celpgpm1fi8[1] __attribute__((aligned(1)));
extern uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
extern uint8_t gm_cnohofor2q6[16] __attribute__((aligned(8)));
extern uint8_t gm_k093ni2ahjb[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
extern uint8_t gm_kh9bgce5chr[40] __attribute__((aligned(8)));
extern uint8_t gm_kif5rfegp72[40] __attribute__((aligned(8)));
extern const uint8_t gm_s055t7hpo88bt[4];
extern const uint8_t gm_s0iid7nkkvf30[55];
extern const uint8_t gm_s0of2eragnise[6];
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s3qd7ldqb9526[29];
extern const uint8_t gm_s3vfg72f8aupk[20];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4hd92lokmtot[4];
extern const uint8_t gm_s4nquq7iq8v5h[29];
extern const uint8_t gm_s4uditudns916[3];
extern const uint8_t gm_s50l7uhgcnu1t[7];
extern const uint8_t gm_s527dqlmmaier[6];
extern const uint8_t gm_s54emlnl8lmoe[38];
extern const uint8_t gm_s684ivkkpni41[7];
extern const uint8_t gm_s68c5jkfvg5dd[86];
extern const uint8_t gm_s6kq1mi4s1g58[3];
extern const uint8_t gm_s7cu0m5qc7arr[43];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s9pcjvamrhbn7[25];
extern const uint8_t gm_sam8dbnfp1kb4[34];
extern const uint8_t gm_sdg1ovmjdibb5[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfeb57kq1r2n4[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgk14ir7dv1gf[6];
extern const uint8_t gm_sgvln0vb20p94[17];
extern const uint8_t gm_sho8l3vae133f[8];
extern const uint8_t gm_sii9tamh3vah6[32];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjbjet56e96co[17];
extern const uint8_t gm_sjsosmp24eqn0[36];
extern const uint8_t gm_skkiv5ia4t4au[39];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrfv83pestqp[31];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_sn1sq59pu45vv[39];
extern const uint8_t gm_sn9nkqgu8isqo[3];
extern const uint8_t gm_snp0oqa4m79hk[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_sojb2hhp0ln7r[31];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_spun41oorb00o[30];
extern const uint8_t gm_squacltecdsom[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3dhhc4laniae[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sreprot2s508e[3];
extern const uint8_t gm_srjc4stueafqk[43];
extern const uint8_t gm_ssfjmtpagd1pu[6];
extern const uint8_t gm_sshi9ffc6nkc4[10];
extern const uint8_t gm_ssulg3jrm3q2v[16];
extern const uint8_t gm_su5tga7voctrl[7];
extern const uint8_t gm_svghqphscbkj1[7];
extern const gm_tmpl gm_tplagt25ql42kpb;
extern const gm_tmpl gm_tpldunbbu9dpisn;
extern const gm_tmpl gm_tplp83ba2hehir9;
extern const gm_tmpl gm_tplqipe054or34v;
extern const gm_tmpl gm_tplvsndsjoh0e6u;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_clear_3ibm3tg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_3ibm3tg F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_gp35vgp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_gp35vgp F;
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
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_h4369ui F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_sl9fc3u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_sl9fc3u F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_51k8cgl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_51k8cgl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_icu3qs9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q5bjns8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q5bjns8 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_e1j40m3 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_967kt0a_2(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5a05gmg F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svghqphscbkj1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplagt25ql42kpb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_j0ggm04 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soohe0i8iafj3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqipe054or34v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uq5dnfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uq5dnfv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s50l7uhgcnu1t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldunbbu9dpisn), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_pq8dr45 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    gm_t4q2b5ibsrdik t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)13ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)11ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)12ULL));
    uint8_t t4 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 5, ((uint8_t *)&t2))) t4 = 1;
    uint8_t t5 = (uint8_t)(t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_1t36jvm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1t36jvm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_51k8cgl();
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
inline __attribute__((aligned(4))) void gm_f_list_93qogsr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_93qogsr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_icu3qs9();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oldiiq9_2();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpi80k4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_glg30gi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_glg30gi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_q5bjns8();
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
inline __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_new_repl_hdsg2o4 F;
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tvcg39brotose t2 = GM_LD(gm_tvcg39brotose, ((uint8_t *)&F.l0));
    gm_trqfvmjtt4sij t3;
    gm_f_list_1t36jvm((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_93qogsr((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t5;
    gm_f_list_e6ie1fq_2((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 172, 0);
    GM_ST(gm_tvcg39brotose, ((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 48), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 88), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 128), t5);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 168), ((uint32_t)0ULL));
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o86m6d7_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_52bt1f1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_52bt1f1 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_51k8cgl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9pv3nuf(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ej78v89 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_icu3qs9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_pcenrov(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f9bu01m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f9bu01m F;
    void * gm_l0; /* l */
    gm_t2s5j3n0guq0t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_q5bjns8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ko1se61(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6e85n8_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t6e85n8_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_oldiiq9_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_967kt0a_2(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_967kt0a_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9pv3nuf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9pv3nuf F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ko1se61(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ko1se61 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_pcenrov F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_mdh37fq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_mdh37fq F;
    void * gm_l0; /* c */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_l1 = t3;
    void * t4 = gm_l1;
    gm_f_deinit_sejdpqv(t4);
    void * t5 = gm_l1;
    gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh9bgce5chr)));
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
inline __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_pb2jqrj F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 48);
    gm_f_clear_3ibm3tg(t3);
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 88);
    gm_f_clear_gp35vgp(t5);
    void * t6 = gm_l0;
    void * t7 = (void *)(((uint8_t *)t6) + 128);
    gm_f_clear_sl9fc3u(t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_show_861qdlm F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    if (t3) {
        gm_b16_8 t4 = gm_l0;
        gm_pf0nmuupvugn(cx, t4, gm_src3dhhc4laniae, 209, 23);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_shown_o0bbr93(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_shown_o0bbr93 F;
    void * gm_l0; /* c */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
    void * t6 = gm_l0;
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 36));
    void * t8 = gm_l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 40));
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 48));
    gm_t9qkh0rsooc8d t12 = gm_l1;
    gm_b16_8 t13 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t3, t5, t7, t9, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_submit_text_7dvfev6 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l1;
    gm_t9feugf5hcdtg t5;
    gm_f_classify_rm38cme((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t6 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t2, t3, &t5);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0gtanrr(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_0gtanrr F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_srjc4stueafqk));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(42));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvsndsjoh0e6u), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nre2qb9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_nre2qb9 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sam8dbnfp1kb4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplp83ba2hehir9), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_command_7gn4uju F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* cmd */
    gm_b16_8 gm_l3; /* arg */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 911u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l8;
    gm_b16_8 t27 = gm_l1;
    gm_l2 = t27;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(0));
    gm_l3 = t28;
    gm_b16_8 t29 = gm_l1;
    gm_l4 = t29;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l5;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                uint64_t t33 = gm_l5;
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t36 = gm_l5;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t36 * 1));
                gm_tf5srh4mp4b3i t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t33);
                GM_ST(uint8_t, ((uint8_t *)&t38 + 8), t37);
                gm_l8 = t38;
                {
                    {
                        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                        gm_l6 = t39;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
                        gm_l7 = t40;
                        uint8_t t41 = gm_l7;
                        uint8_t t42 = gm_f_is_space_pq8dr45(t41);
                        if (t42) {
                            void * t43 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t45 = gm_l6;
                            uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t47 = (uintptr_t)t45;
                            if (t46 > t47 || t47 > (uintptr_t)t44) gm_trap_slice(t46, t47, t44, gm_src3dhhc4laniae, 454, 19);
                            uint64_t t48 = ((uintptr_t)t45) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t49;
                            GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t43 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48));
                            gm_l2 = t49;
                            void * t50 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t52 = gm_l6;
                            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                            if (o54) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 455, 33);
                            uint64_t t55 = r53;
                            uint64_t t56 = (uintptr_t)t55;
                            uint64_t t57 = (uintptr_t)t51;
                            if (t56 > t57 || t57 > (uintptr_t)t51) gm_trap_slice(t56, t57, t51, gm_src3dhhc4laniae, 455, 28);
                            uint64_t t58 = ((uintptr_t)t51) - (uintptr_t)t55;
                            gm_b16_8 t59;
                            GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t50 + (uintptr_t)t55 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58));
                            gm_b16_8 t60 = gm_f_trim_fdrfe6i(t59);
                            gm_l3 = t60;
                            goto gm_lb1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 452, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l5;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l5 = t64;
        }
    }
gm_lb1:;
    {
        {
            {
                {
                    gm_b16_8 t65 = gm_l2;
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sdg1ovmjdibb5));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(5));
                    uint8_t t67 = (gm_eq0iqiubfm7e(((uint8_t *)&t65), ((uint8_t *)&t66)));
                    if (!t67) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    gm_b16_8 t68 = gm_l2;
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sreprot2s508e));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(2));
                    uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                    if (!t70) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    gm_b16_8 t71 = gm_l2;
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sgk14ir7dv1gf));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(5));
                    uint8_t t73 = (gm_eq0iqiubfm7e(((uint8_t *)&t71), ((uint8_t *)&t72)));
                    if (!t73) {
                        goto gm_lb11;
                    }
                    goto gm_lb8;
                }
gm_lb11:;
                goto gm_lb7;
            }
gm_lb8:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb7:;
        {
            {
                {
                    gm_b16_8 t74 = gm_l2;
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s527dqlmmaier));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(5));
                    uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
                    if (!t76) {
                        goto gm_lb14;
                    }
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    gm_b16_8 t77 = gm_l2;
                    gm_b16_8 t78;
                    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s4uditudns916));
                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(2));
                    uint8_t t79 = (gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                    if (!t79) {
                        goto gm_lb15;
                    }
                    goto gm_lb13;
                }
gm_lb15:;
                {
                    gm_b16_8 t80 = gm_l2;
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_s6kq1mi4s1g58));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(2));
                    uint8_t t82 = (gm_eq0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t81)));
                    if (!t82) {
                        goto gm_lb16;
                    }
                    goto gm_lb13;
                }
gm_lb16:;
                goto gm_lb12;
            }
gm_lb13:;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)gm_c5dv10uirua));
            gm_pfmaojsuds6a(cx, t83, gm_src3dhhc4laniae, 461, 32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb12:;
        {
            gm_b16_8 t84 = gm_l2;
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s684ivkkpni41));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(6));
            uint8_t t86 = (gm_eq0iqiubfm7e(((uint8_t *)&t84), ((uint8_t *)&t85)));
            if (!t86) {
                goto gm_lb17;
            }
            void * t87 = gm_l0;
            gm_f_reset_pb2jqrj(t87);
            goto gm_lb6;
        }
gm_lb17:;
        {
            gm_b16_8 t88 = gm_l2;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_ssfjmtpagd1pu));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(5));
            uint8_t t90 = (gm_eq0iqiubfm7e(((uint8_t *)&t88), ((uint8_t *)&t89)));
            if (!t90) {
                goto gm_lb18;
            }
            void * t91 = gm_l0;
            gm_b0_1 t92 = gm_f_list_session_6f2qqqv((void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb18:;
        {
            {
                {
                    gm_b16_8 t93 = gm_l2;
                    gm_b16_8 t94;
                    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s0of2eragnise));
                    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(5));
                    uint8_t t95 = (gm_eq0iqiubfm7e(((uint8_t *)&t93), ((uint8_t *)&t94)));
                    if (!t95) {
                        goto gm_lb21;
                    }
                    goto gm_lb20;
                }
gm_lb21:;
                {
                    gm_b16_8 t96 = gm_l2;
                    gm_b16_8 t97;
                    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sn9nkqgu8isqo));
                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
                    uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
                    if (!t98) {
                        goto gm_lb22;
                    }
                    goto gm_lb20;
                }
gm_lb22:;
                goto gm_lb19;
            }
gm_lb20:;
            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t100 = ((t99 == ((uint64_t)0ULL)));
            if (t100) {
                if (gm_pf454qfrpt6r(cx, gm_sjsosmp24eqn0, 35, gm_src3dhhc4laniae, 465, 31)) {
                    goto gm_lad;
                }
            } else {
                void * t101 = gm_l0;
                gm_b16_8 t102 = gm_l3;
                gm_b0_1 t103 = gm_f_type_of_b1a5sco((void *)NULL, cx, NULL, t101, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb6;
        }
gm_lb19:;
        {
            gm_b16_8 t104 = gm_l2;
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_snp0oqa4m79hk));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(5));
            uint8_t t106 = (gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
            if (!t106) {
                goto gm_lb23;
            }
            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t108 = ((t107 == ((uint64_t)0ULL)));
            if (t108) {
                if (gm_pf454qfrpt6r(cx, gm_s4nquq7iq8v5h, 28, gm_src3dhhc4laniae, 468, 31)) {
                    goto gm_lad;
                }
            } else {
                void * t109 = gm_l0;
                gm_b16_8 t110 = gm_l3;
                uint8_t t111 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t109, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb6;
        }
gm_lb23:;
        {
            gm_b16_8 t112 = gm_l2;
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sho8l3vae133f));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(7));
            uint8_t t114 = (gm_eq0iqiubfm7e(((uint8_t *)&t112), ((uint8_t *)&t113)));
            if (!t114) {
                goto gm_lb24;
            }
            goto gm_lb6;
        }
gm_lb24:;
        {
            gm_b16_8 t115 = gm_l2;
            if (gm_pf454qfrpt6r(cx, gm_s9pcjvamrhbn7, 24, gm_src3dhhc4laniae, 471, 14)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t115) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t115)), GM_LD(uintptr_t, ((uint8_t *)&t115) + 8), gm_src3dhhc4laniae, 471, 14))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_sii9tamh3vah6, 31, gm_src3dhhc4laniae, 471, 14)) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb25:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 459, 5);
    }
gm_lb6:;
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
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_command_run_06r7r92 F;
    uint8_t gm_l2; /* interactive */
    uint8_t gm_l3; /* quiet */
    gm_b16_8 gm_l5; /* path */
    gm_tl572d7579hns gm_l7; /* next */
    gm_b16_8 gm_l8; /* l */
    gm_b16_8 gm_l9; /* line_text */
    gm_b16_8 gm_l10; /* line */
    gm_b16_8 gm_l12; /* text */
    GM_STACK_CHECK(&F, 1127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_b16_8 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    uint8_t gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    {
        memmove(((uint8_t *)&F.l1), ((uint8_t *)&F.l0), 48);
    }
    int32_t t13 = gm_x_isatty_dhpcvep(((int32_t)0LL));
    uint8_t t14 = (!(t13 == ((int32_t)0LL)));
    if (t14) {
        int32_t t15 = gm_x_isatty_dhpcvep(((int32_t)1LL));
        uint8_t t16 = (!(t15 == ((int32_t)0LL)));
        gm_l13 = t16;
    } else {
        gm_l13 = ((uint8_t)0);
    }
    uint8_t t17 = gm_l13;
    gm_l2 = t17;
    uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
    if (t18) {
        gm_l14 = ((uint8_t)1);
    } else {
        uint8_t t19 = gm_l2;
        uint8_t t20 = (uint8_t)!t19;
        gm_l14 = t20;
    }
    uint8_t t21 = gm_l14;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t21);
    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
    gm_l3 = t22;
    {
        gm_tec68k60rd12v t23;
        gm_f_new_repl_hdsg2o4((void *)&t23, (void *)NULL, cx, NULL, &(*(gm_tvcg39brotose *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t23;
    }
    {
        {
            uint8_t t24 = (uint8_t)(((((uint8_t *)&F.l1) + 8))[16] != 0);
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
            gm_l5 = t25;
            void * t26 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t27 = gm_l5;
            uint8_t t28 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = (uint8_t)!t28;
            if (t29) {
                F.ret = ((uint8_t)1ULL);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t30 = gm_l3;
    uint8_t t31 = (uint8_t)!t30;
    if (t31) {
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 32));
        if (gm_pf1m4b8q4e78(cx, gm_su5tga7voctrl, 6, gm_src3dhhc4laniae, 517, 20)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t32) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t32)), GM_LD(uintptr_t, ((uint8_t *)&t32) + 8), gm_src3dhhc4laniae, 517, 20))) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_s7cu0m5qc7arr, 42, gm_src3dhhc4laniae, 517, 20)) {
            goto gm_lad;
        }
    }
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_uvfu1k1((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t33;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb3;
            }
            {
                uint8_t t34 = gm_l3;
                uint8_t t35 = (uint8_t)!t34;
                if (t35) {
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
                    if (t37) {
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s4hd92lokmtot));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
                        gm_l15 = t38;
                    } else {
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sfeb57kq1r2n4));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(3));
                        gm_l15 = t39;
                    }
                    gm_b16_8 t40 = gm_l15;
                    gm_pfmaojsuds6a(cx, t40, gm_src3dhhc4laniae, 520, 24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_f_read_line_1oksar8((void *)&gm_l7, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t42 = (uint8_t)(t41 == ((uint8_t)1ULL));
                        if (!t42) {
                            goto gm_lb7;
                        }
                        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                        uint8_t t44 = (t43 > ((uint64_t)0ULL));
                        if (t44) {
                            void * t45 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                            gm_f_dupe_g9iqpvn((void *)&gm_l16, (void *)NULL, cx, NULL, t47, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k093ni2ahjb)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src3dhhc4laniae, 524, 58);
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                            gm_b0_1 t51 = gm_f_submit_text_7dvfev6((void *)NULL, cx, NULL, t45, t50);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb3;
                    }
gm_lb7:;
                    {
                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)2ULL));
                        if (!t53) {
                            goto gm_lb8;
                        }
                        if (gm_pf454qfrpt6r(cx, gm_s0iid7nkkvf30, 54, gm_src3dhhc4laniae, 528, 17)) {
                            goto gm_lad;
                        }
                        goto gm_lb5;
                    }
gm_lb8:;
                    {
                        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                        if (!t55) {
                            goto gm_lb9;
                        }
                        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                        gm_l8 = t56;
                        gm_b16_8 t57 = gm_l8;
                        gm_l9 = t57;
                        goto gm_lb6;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 522, 22);
                }
gm_lb6:;
                gm_b16_8 t58 = gm_l9;
                gm_b16_8 t59 = gm_f_trim_newlines_9rj3i73(t58);
                gm_l10 = t59;
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                uint8_t t61 = ((t60 == ((uint64_t)0ULL)));
                if (t61) {
                    gm_b16_8 t62 = gm_l10;
                    gm_b16_8 t63 = gm_f_trim_start_179crak(t62);
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s39mljh669qgl));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
                    uint8_t t65 = gm_f_starts_with_6vvt01e(t63, t64);
                    if (t65) {
                        void * t66 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t67 = gm_l10;
                        gm_b16_8 t68 = gm_f_trim_fdrfe6i(t67);
                        uint8_t t69 = gm_f_command_7gn4uju((void *)NULL, cx, NULL, t66, t68);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t70 = (uint8_t)!t69;
                        if (t70) {
                            goto gm_lb3;
                        }
                        goto gm_lb5;
                    }
                    gm_b16_8 t71 = gm_l10;
                    uint8_t t72 = gm_f_is_blank_asoolmf((void *)NULL, cx, NULL, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t72) {
                        goto gm_lb5;
                    }
                } else {
                    gm_b16_8 t73 = gm_l10;
                    gm_b16_8 t74 = gm_f_trim_fdrfe6i(t73);
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sho8l3vae133f));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(7));
                    uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
                    if (t76) {
                        void * t77 = (void *)((uint8_t *)&F.l6);
                        gm_f_clear_h4369ui(t77);
                        goto gm_lb5;
                    }
                }
                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                uint8_t t79 = (t78 > ((uint64_t)0ULL));
                if (t79) {
                    void * t80 = (void *)((uint8_t *)&F.l6);
                    gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)10ULL));
                    gm_l17 = t81;
                    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src3dhhc4laniae, 544, 36);
                }
                void * t83 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t84 = gm_l10;
                gm_tftb1odc739t5 t85 = gm_f_extend_0cs85gs(t83, t84);
                gm_l18 = t85;
                uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src3dhhc4laniae, 545, 9);
                {
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                    gm_f_classify_rm38cme((void *)&F.l11, (void *)NULL, cx, NULL, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 56));
                        if (!t89) {
                            goto gm_lb11;
                        }
                        gm_b16_8 t90 = gm_l10;
                        gm_b16_8 t91 = gm_f_trim_fdrfe6i(t90);
                        gm_l20 = t91;
                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint8_t t93 = ((t92 == ((uint64_t)0ULL)));
                        if (t93) {
                            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                            uint8_t t96 = gm_f_open_brackets_hicehud((void *)NULL, cx, NULL, t95);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t97 = (uint8_t)!t96;
                            gm_l19 = t97;
                        } else {
                            gm_l19 = ((uint8_t)0);
                        }
                        uint8_t t98 = gm_l19;
                        uint8_t t99 = (uint8_t)!t98;
                        if (!t99) {
                            goto gm_lb11;
                        }
                        goto gm_lb5;
                    }
gm_lb11:;
                }
gm_lb10:;
                {
                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                    gm_f_dupe_g9iqpvn((void *)&gm_l21, (void *)NULL, cx, NULL, t101, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kif5rfegp72)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src3dhhc4laniae, 549, 17);
                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                gm_l12 = t103;
                void * t104 = (void *)((uint8_t *)&F.l6);
                gm_f_clear_h4369ui(t104);
                {
                    void * t105 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t106 = gm_l12;
                    gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                    gm_b0_1 t108 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t105, t107, &(*(gm_t9feugf5hcdtg *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
gm_lb5:;
        }
    }
gm_lb3:;
    F.ret = ((uint8_t)0ULL);
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
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6) {
    struct gm_fr_compile_rj09gp7 F;
    void * gm_l0; /* r */
    gm_th2lqbo77pvsc gm_l1; /* new_decls */
    gm_b16_8 gm_l2; /* replaced */
    gm_b16_8 gm_l3; /* replay */
    gm_b16_8 gm_l5; /* effects */
    uint8_t gm_l6; /* sentinel */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t6j6smj7qoctu gm_l10; /* c */
    uint8_t gm_l11; /* skip */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l14; /* n */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* x */
    uint64_t gm_l18; /* lo */
    gm_b16_8 gm_l19; /* input */
    gm_b16_8 gm_l20; /* d */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* t */
    GM_STACK_CHECK(&F, 1343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    {
        gm_trqfvmjtt4sij t28;
        gm_f_list_uvfu1k1((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t28;
    }
    void * t29 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)gm_cnohofor2q6));
    gm_tftb1odc739t5 t31 = gm_f_extend_0cs85gs(t29, t30);
    gm_l26 = t31;
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src3dhhc4laniae, 146, 5);
    void * t33 = gm_l0;
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 48));
    gm_l8 = t34;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t35 = gm_l9;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb1;
            }
            {
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t40 = gm_l9;
                {
                    {
                        memmove(((uint8_t *)&gm_l10), (((uint8_t *)t38) + (uintptr_t)t40 * 32), 32);
                        gm_l11 = ((uint8_t)0);
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                        gm_l12 = t41;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t42 = gm_l13;
                                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t44 = (t42 < t43);
                                if (!t44) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t45 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t47 = gm_l13;
                                    {
                                        {
                                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)t47 * 16));
                                            gm_l14 = t48;
                                            gm_b16_8 t49 = gm_l2;
                                            gm_l15 = t49;
                                            gm_l16 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t50 = gm_l16;
                                                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                    uint8_t t52 = (t50 < t51);
                                                    if (!t52) {
                                                        goto gm_lb11;
                                                    }
                                                    {
                                                        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                        uint64_t t55 = gm_l16;
                                                        {
                                                            {
                                                                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)t55 * 16));
                                                                gm_l17 = t56;
                                                                gm_b16_8 t57 = gm_l14;
                                                                gm_b16_8 t58 = gm_l17;
                                                                uint8_t t59 = (gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                                                if (t59) {
                                                                    gm_l11 = ((uint8_t)1);
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb15:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 150, 13);
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    uint64_t t60 = gm_l16;
                                                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                                                    uint64_t t63 = r61;
                                                    gm_l16 = t63;
                                                }
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 149, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t64 = gm_l13;
                                uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                                uint64_t t67 = r65;
                                gm_l13 = t67;
                            }
                        }
gm_lb6:;
                        uint8_t t68 = gm_l11;
                        if (t68) {
                            goto gm_lb3;
                        }
                        void * t69 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                        gm_tftb1odc739t5 t71 = gm_f_extend_0cs85gs(t69, t70);
                        gm_l27 = t71;
                        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src3dhhc4laniae, 155, 9);
                        void * t73 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s8dbdqj1sqibj));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t73, t74);
                        gm_l28 = t75;
                        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src3dhhc4laniae, 156, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 147, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t77 = gm_l9;
            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
            uint64_t t80 = r78;
            gm_l9 = t80;
        }
    }
gm_lb1:;
    gm_l18 = ((uint64_t)0ULL);
    gm_b16_8 t81;
    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(0));
    gm_l19 = t81;
    {
        {
            uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
            if (!t82) {
                goto gm_lb17;
            }
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_l20 = t83;
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
            gm_l18 = t84;
            gm_b16_8 t85 = gm_l20;
            gm_l19 = t85;
            void * t86 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t87 = gm_l20;
            gm_tftb1odc739t5 t88 = gm_f_extend_0cs85gs(t86, t87);
            gm_l29 = t88;
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src3dhhc4laniae, 163, 9);
            void * t90 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t92 = gm_f_extend_0cs85gs(t90, t91);
            gm_l30 = t92;
            uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src3dhhc4laniae, 164, 9);
            goto gm_lb16;
        }
gm_lb17:;
    }
gm_lb16:;
    void * t94 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t95 = gm_l5;
    gm_tnpifv20irnph t96;
    GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t95);
    gm_tftb1odc739t5 t97 = gm_f_write_0gtanrr(t94, t96);
    gm_l31 = t97;
    uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src3dhhc4laniae, 166, 5);
    gm_b16_8 t99 = gm_l3;
    gm_l21 = t99;
    gm_l22 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t100 = gm_l22;
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
            uint8_t t102 = (t100 < t101);
            if (!t102) {
                goto gm_lb18;
            }
            {
                void * t103 = GM_LD(void *, ((uint8_t *)&gm_l21));
                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint64_t t105 = gm_l22;
                {
                    {
                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)t103) + (uintptr_t)t105 * 16));
                        gm_l23 = t106;
                        void * t107 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t108;
                        GM_ST(void *, (uint8_t *)&t108, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t109 = gm_f_extend_0cs85gs(t107, t108);
                        gm_l32 = t109;
                        uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                        if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src3dhhc4laniae, 168, 9);
                        void * t111 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t112 = gm_l23;
                        gm_tftb1odc739t5 t113 = gm_f_extend_0cs85gs(t111, t112);
                        gm_l33 = t113;
                        uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src3dhhc4laniae, 169, 9);
                        void * t115 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t116 = gm_f_push_t83rg2m(t115, ((uint8_t)10ULL));
                        gm_l34 = t116;
                        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src3dhhc4laniae, 170, 9);
                        goto gm_lb21;
                    }
gm_lb22:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 167, 5);
                }
gm_lb21:;
            }
gm_lb20:;
            uint64_t t118 = gm_l22;
            uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
            uint64_t t121 = r119;
            gm_l22 = t121;
        }
    }
gm_lb18:;
    {
        {
            uint8_t t122 = gm_l6;
            if (!t122) {
                goto gm_lb24;
            }
            uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t124 = (t123 > ((uint64_t)0ULL));
            if (!t124) {
                goto gm_lb24;
            }
            void * t125 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)gm_cdgbrlsjvrb));
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)gm_cdgbrlsjvrb));
            gm_t6j6smj7qoctu t128;
            GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t126);
            GM_ST(gm_b16_8, ((uint8_t *)&t128 + 16), t127);
            gm_tftb1odc739t5 t129 = gm_f_write_nre2qb9(t125, &t128);
            gm_l35 = t129;
            uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src3dhhc4laniae, 173, 9);
            goto gm_lb23;
        }
gm_lb24:;
    }
gm_lb23:;
    {
        {
            uint8_t t131 = (uint8_t)((((uint8_t *)&F.l4))[48] != 0);
            if (!t131) {
                goto gm_lb26;
            }
            memmove(((uint8_t *)&F.l24), ((uint8_t *)&F.l4), 48);
            void * t132 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
            gm_tftb1odc739t5 t134 = gm_f_extend_0cs85gs(t132, t133);
            gm_l36 = t134;
            uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
            if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src3dhhc4laniae, 176, 9);
            uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
            gm_l18 = t136;
            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_l19 = t137;
            void * t138 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t138, t139);
            gm_l37 = t140;
            uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
            if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src3dhhc4laniae, 179, 9);
            void * t142 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 32));
            gm_tftb1odc739t5 t144 = gm_f_extend_0cs85gs(t142, t143);
            gm_l38 = t144;
            uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
            if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src3dhhc4laniae, 180, 9);
            goto gm_lb25;
        }
gm_lb26:;
    }
gm_lb25:;
    void * t146 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t147;
    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sikpqtja8pqpv));
    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t148 = gm_f_extend_0cs85gs(t146, t147);
    gm_l39 = t148;
    uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
    if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src3dhhc4laniae, 182, 5);
    {
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        void * t152 = gm_l0;
        gm_t9qkh0rsooc8d t153 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t152) + 1));
        uint64_t t154 = gm_l18;
        uint32_t t155 = (uint32_t)t154;
        gm_b16_8 t156 = gm_l19;
        gm_f_check_source_nh9qcnk((void *)&F.l25, (void *)NULL, cx, NULL, t151, t153, t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t157 = (void *)((uint8_t *)&F.l7);
    gm_f_deinit_r70pji5(t157);
    {
        memmove(gm_out, ((uint8_t *)&F.l25), 64);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_list_session_6f2qqqv F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t6j6smj7qoctu gm_l3; /* c */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 48));
    gm_l1 = t13;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t19 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&gm_l3), (((uint8_t *)t17) + (uintptr_t)t19 * 32), 32);
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t20)), GM_LD(uintptr_t, ((uint8_t *)&t20) + 8), gm_src3dhhc4laniae, 442, 31))) {
                            goto gm_lad;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 442, 31)) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 442, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l2;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l2 = t24;
        }
    }
gm_lb1:;
    void * t25 = gm_l0;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 88));
    gm_l4 = t26;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l5;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb6;
            }
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t32 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t30) + (uintptr_t)t32 * 40), 40);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t33)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8), gm_src3dhhc4laniae, 443, 31))) {
                            goto gm_lad;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 443, 31)) {
                            goto gm_lad;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 443, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t34 = gm_l5;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l5 = t37;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_file_3kdl78m F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* path_text */
    gm_b16_8 gm_l2; /* path */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* text */
    gm_b16_8 gm_l7; /* items */
    uint8_t gm_l9; /* has_main */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint8_t gm_l13; /* is_main */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* n */
    gm_b16_8 gm_l17; /* load_text */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l22; /* msg */
    GM_STACK_CHECK(&F, 1167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tgvh15qshf643 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_th2lqbo77pvsc gm_l27;
    gm_b16_8 t17 = gm_l1;
    gm_b16_8 t18 = gm_f_trim_fdrfe6i(t17);
    gm_l2 = t18;
    {
        gm_b16_8 t19 = gm_l2;
        gm_f_read_all_r9rh9qd((void *)&gm_l23, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t20) {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_l4 = t21;
    } else {
        gm_tj5jn79kq9pnm t22 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l23) + 8));
        gm_l3 = t22;
        gm_b16_8 t23 = gm_l2;
        gm_tj5jn79kq9pnm t24 = gm_l3;
        gm_b16_8 t25 = gm_f_read_error_text_1lgi2bl((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 406, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t23) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t23)), GM_LD(uintptr_t, ((uint8_t *)&t23) + 8), gm_src3dhhc4laniae, 406, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_src3dhhc4laniae, 406, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t25) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t25)), GM_LD(uintptr_t, ((uint8_t *)&t25) + 8), gm_src3dhhc4laniae, 406, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 406, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t26 = gm_l4;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    uint8_t t28 = gm_f_utf8_valid_a8oc1t8(t27);
    uint8_t t29 = (uint8_t)!t28;
    if (t29) {
        gm_b16_8 t30 = gm_l2;
        if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 410, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t30) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t30)), GM_LD(uintptr_t, ((uint8_t *)&t30) + 8), gm_src3dhhc4laniae, 410, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s54emlnl8lmoe, 37, gm_src3dhhc4laniae, 410, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t31 = gm_l4;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    gm_l5 = t32;
    {
        gm_b16_8 t33 = gm_l5;
        gm_f_classify_rm38cme((void *)&F.l6, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t34 = (uint8_t)((((uint8_t *)&F.l6))[16] != 0);
    if (t34) {
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l7 = t35;
    } else {
        gm_b16_8 t36 = gm_l5;
        void * t37 = gm_l0;
        gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t37) + 2));
        gm_b16_8 t39 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t36, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t40 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        gm_trqfvmjtt4sij t41;
        gm_f_list_f0gjjbh_2((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t41;
    }
    gm_l9 = ((uint8_t)0);
    gm_b16_8 t42 = gm_l7;
    gm_l10 = t42;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t43 = gm_l11;
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t45 = (t43 < t44);
            if (!t45) {
                goto gm_lb1;
            }
            {
                void * t46 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t48 = gm_l11;
                {
                    {
                        memmove(((uint8_t *)&F.l12), (((uint8_t *)t46) + (uintptr_t)t48 * 40), 40);
                        gm_l13 = ((uint8_t)0);
                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                        gm_l14 = t49;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t50 = gm_l15;
                                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint8_t t52 = (t50 < t51);
                                if (!t52) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t53 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint64_t t55 = gm_l15;
                                    {
                                        {
                                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)t55 * 16));
                                            gm_l16 = t56;
                                            gm_b16_8 t57 = gm_l16;
                                            gm_b16_8 t58;
                                            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sog4rksrmsqd2));
                                            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(4));
                                            uint8_t t59 = (gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                            if (t59) {
                                                gm_l13 = ((uint8_t)1);
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 423, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t60 = gm_l15;
                                uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                                uint64_t t63 = r61;
                                gm_l15 = t63;
                            }
                        }
gm_lb6:;
                        uint8_t t64 = gm_l13;
                        if (t64) {
                            gm_l9 = ((uint8_t)1);
                        } else {
                            void * t65 = (void *)((uint8_t *)&F.l8);
                            gm_tftb1odc739t5 t66 = gm_f_push_1e500nc_2(t65, &(*(gm_tk9n3vlila9qg *)((uint8_t *)&F.l12)));
                            gm_l24 = t66;
                            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src3dhhc4laniae, 426, 47);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 421, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t68 = gm_l11;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l11 = t71;
        }
    }
gm_lb1:;
    gm_b16_8 t72 = gm_l5;
    gm_l17 = t72;
    uint8_t t73 = gm_l9;
    if (t73) {
        gm_b16_8 t74 = gm_l2;
        if (gm_pf454qfrpt6r(cx, gm_sjbjet56e96co, 16, gm_src3dhhc4laniae, 430, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t74) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t74)), GM_LD(uintptr_t, ((uint8_t *)&t74) + 8), gm_src3dhhc4laniae, 430, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_ssulg3jrm3q2v, 15, gm_src3dhhc4laniae, 430, 9)) {
            goto gm_lad;
        }
        {
            gm_trqfvmjtt4sij t75;
            gm_f_list_epvducg((void *)&t75, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l18 = t75;
        }
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_l19 = t76;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t77 = gm_l20;
                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t79 = (t77 < t78);
                if (!t79) {
                    goto gm_lb11;
                }
                {
                    void * t80 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t82 = gm_l20;
                    {
                        {
                            memmove(((uint8_t *)&F.l21), (((uint8_t *)t80) + (uintptr_t)t82 * 40), 40);
                            void * t83 = (void *)((uint8_t *)&F.l18);
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 16));
                            gm_tftb1odc739t5 t85 = gm_f_push_3ef746b(t83, t84);
                            gm_l25 = t85;
                            uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src3dhhc4laniae, 432, 31);
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 432, 9);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t87 = gm_l20;
                uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                uint64_t t90 = r88;
                gm_l20 = t90;
            }
        }
gm_lb11:;
        {
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l26, (void *)NULL, cx, NULL, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src3dhhc4laniae, 433, 21);
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_l17 = t96;
    }
    {
        void * t97 = gm_l0;
        gm_b16_8 t98 = gm_l17;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_f_try_decls_23vcohd((void *)&gm_l27, (void *)NULL, cx, NULL, t97, t98, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
    if (t101) {
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
        gm_l22 = t102;
    } else {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_b16_8 t103 = gm_l22;
    gm_b0_1 t104 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t103);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
extern __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_options_9st61k7 F;
    gm_b16_8 gm_l0; /* args */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l5; /* value */
    gm_b16_8 gm_l6; /* message */
    GM_STACK_CHECK(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tepv5jklv667k gm_l7;
    gm_tabb710d9mq2e gm_l8;
    uint8_t gm_l9;
    {
        gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_celpgpm1fi8));
        gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_ckbflbrril1));
        gm_th2lqbo77pvsc t19;
        memset(&t19, 0, sizeof t19);
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)gm_c7983l5vkhl));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 3, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)0));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1 + 1), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1 + 2), t18);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l1 + 8), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l1 + 32), t20);
    }
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l2;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t26 = gm_l2;
                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t24) + (uintptr_t)t26 * 16));
                gm_l3 = t27;
                {
                    gm_b16_8 t28 = gm_l3;
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s4chbmodjkinv));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(6));
                    gm_b16_8 t30;
                    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s3a1oio07ri1p));
                    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
                    gm_t6j6smj7qoctu t31;
                    GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t29);
                    GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t30);
                    uint8_t t32 = 0;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t31) + 0, ((uint8_t *)&t28))) t32 = 1;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t31) + 16, ((uint8_t *)&t28))) t32 = 1;
                    uint8_t t33 = (uint8_t)(t32);
                    if (t33) {
                        memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0), ((uint8_t)1ULL));
                        goto gm_fdone;
                    }
                }
                {
                    gm_b16_8 t34 = gm_l0;
                    uint64_t t35 = gm_l2;
                    gm_f_scan_ll1gqkn((void *)&gm_l7, (void *)NULL, cx, NULL, t34, t35, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                        if (!t37) {
                            goto gm_lb5;
                        }
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l7) + 8), 40);
                        uint8_t t38 = (uint8_t)(((((uint8_t *)&F.l4) + 8))[16] != 0);
                        if (t38) {
                            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            gm_l5 = t39;
                        } else {
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
                            gm_l5 = t40;
                        }
                        {
                            {
                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)1ULL));
                                if (!t42) {
                                    goto gm_lb7;
                                }
                                GM_ST(uint8_t, ((uint8_t *)&F.l1), ((uint8_t)1));
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)4ULL));
                                if (!t44) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t45 = gm_l5;
                                gm_t9qkh0rsooc8d t46 = gm_f_mode_of_pq04he2(t45);
                                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 1), t46);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
                                if (!t48) {
                                    goto gm_lb9;
                                }
                                gm_b16_8 t49 = gm_l5;
                                gm_t9qkh0rsooc8d t50 = gm_f_format_of_nbvl1cj(t49);
                                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 2), t50);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                goto gm_lb6;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 63, 17);
                        }
gm_lb6:;
                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                        gm_l2 = t51;
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)2ULL));
                        if (!t53) {
                            goto gm_lb11;
                        }
                        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                        gm_l6 = t54;
                        gm_b16_8 t55 = gm_l6;
                        memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0), ((uint8_t)2ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8), t55);
                        goto gm_fdone;
                    }
gm_lb11:;
                    {
                        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t57 = (uint8_t)(t56 == ((uint8_t)1ULL));
                        if (!t57) {
                            goto gm_lb12;
                        }
                        goto gm_lb4;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 60, 9);
                }
gm_lb4:;
                uint64_t t58 = gm_l2;
                uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                if (o60) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 75, 9);
                uint64_t t61 = r59;
                gm_l2 = t61;
                gm_b16_8 t62 = gm_l3;
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
                uint8_t t64 = gm_f_starts_with_6vvt01e(t62, t63);
                if (t64) {
                    {
                        gm_b16_8 t65 = gm_l3;
                        gm_tnpifv20irnph t66;
                        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                        gm_f_format_j0ggm04((void *)&gm_l8, (void *)NULL, cx, NULL, t66);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src3dhhc4laniae, 76, 45);
                    {
                        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                        memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0), ((uint8_t)2ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8), t69);
                        goto gm_fdone;
                    }
                }
                {
                    {
                        uint8_t t70 = (uint8_t)(((((uint8_t *)&F.l1) + 8))[16] != 0);
                        if (t70) {
                            goto gm_lb14;
                        }
                        gm_l9 = ((uint8_t)0);
                        goto gm_lb13;
                    }
gm_lb14:;
                    {
                        gm_l9 = ((uint8_t)1);
                        goto gm_lb13;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 77, 12);
                }
gm_lb13:;
                uint8_t t71 = gm_l9;
                if (t71) {
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_slrfv83pestqp));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(30));
                    memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0), ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8), t72);
                    goto gm_fdone;
                }
                {
                    gm_b16_8 t73 = gm_l3;
                    gm_th2lqbo77pvsc t74;
                    GM_ST(uint8_t, (uint8_t *)&t74 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t74 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t74), t73);
                    ((uint8_t *)&t74)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l1) + 8), t74);
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_tvcg39brotose t75 = GM_LD(gm_tvcg39brotose, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_tvcg39brotose, ((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8), t75);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_error_text_1lgi2bl F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    int32_t gm_l1; /* code */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 4) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)13ULL));
            if (!t7) {
                goto gm_lb2;
            }
            int32_t t8 = GM_LD(int32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
            gm_l1 = t8;
            int32_t t9 = gm_l1;
            uint8_t t10 = ((t9 == ((int32_t)21LL)));
            if (t10) {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s3qd7ldqb9526));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(28));
                F.ret = t11;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tj5jn79kq9pnm t12 = gm_l0;
    gm_b16_8 t13 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_read_line_1oksar8 F;
    gm_tivanu2mfcv6o gm_l1; /* one */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_thf5kr0is9u9i gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_trqfvmjtt4sij t5;
        gm_f_list_uvfu1k1((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l0 = t5;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t6 = (void *)((uint8_t *)&gm_l1);
                uint64_t t7 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t8;
                GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t6 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7));
                gm_node *pn9 = gm_find(cx, 1018230276);
                if (!pn9) gm_trap("no handler installed for effect `In`", gm_src3dhhc4laniae, 490, 14);
                gm_thf5kr0is9u9i t10;
                t10 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn9->clauses[0])(pn9->env, pn9->hctx, pn9, t8);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l3 = t10;
                uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (t11) {
                    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    gm_l2 = t12;
                } else {
                    gm_l2 = ((uint64_t)0ULL);
                }
                uint64_t t13 = gm_l2;
                uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
                if (t14) {
                    goto gm_lb1;
                }
                void * t15 = (void *)((uint8_t *)&F.l0);
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + (uintptr_t)((uint64_t)0ULL) * 1));
                gm_tftb1odc739t5 t17 = gm_f_push_t83rg2m(t15, t16);
                gm_l4 = t17;
                uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src3dhhc4laniae, 492, 9);
                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t20 = ((t19 == ((uint8_t)10ULL)));
                if (t20) {
                    goto gm_lb1;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 8));
    uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
    if (t22) {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
        goto gm_fdone;
    }
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&F.l0));
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    uint8_t t25 = gm_f_utf8_valid_a8oc1t8(t24);
    uint8_t t26 = (uint8_t)!t25;
    if (t26) {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)2ULL));
        goto gm_fdone;
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l0));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t28);
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
inline __attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_renamed_vars_fho5hvj F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK(&F, 231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_e6ie1fq_2((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t7;
    }
    void * t8 = gm_l0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 128));
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t13) + (uintptr_t)t15 * 32), 32);
                        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                        uint8_t t18 = (!gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
                        if (t18) {
                            void * t19 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t20 = gm_f_push_t6e85n8_2(t19, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l4)));
                            gm_l5 = t20;
                            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                            if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src3dhhc4laniae, 202, 39);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 201, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l3;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l3 = t25;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_replay_texts_3te9pi7 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* pure_only */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t9;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 88));
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
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t15) + (uintptr_t)t17 * 40), 40);
                        {
                            {
                                uint8_t t18 = gm_l1;
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 32));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t20 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t22 = gm_f_push_3ef746b(t20, t21);
                        gm_l6 = t22;
                        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src3dhhc4laniae, 193, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 191, 5);
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
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
extern __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_set_var_pjsp5eq F;
    void * gm_l0; /* vars */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* internal */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_l4 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
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
                        void * t13 = gm_l0;
                        void * t14 = GM_LD(void *, ((uint8_t *)t13));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t13) + 8));
                        uint64_t t16 = gm_l5;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src3dhhc4laniae, 361, 12);
                        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 32));
                        gm_b16_8 t18 = gm_l1;
                        uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                        if (t19) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l5;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src3dhhc4laniae, 362, 13);
                            gm_b16_8 t24 = gm_l2;
                            GM_ST(gm_b16_8, ((((uint8_t *)t21) + (uintptr_t)t23 * 32) + 16), t24);
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 360, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l4;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l4 = t28;
        }
    }
gm_lb1:;
    void * t29 = gm_l0;
    gm_b16_8 t30 = gm_l1;
    gm_b16_8 t31 = gm_l2;
    gm_t6j6smj7qoctu t32;
    GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
    GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
    gm_tftb1odc739t5 t33 = gm_f_push_t6e85n8_2(t29, &t32);
    gm_l6 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src3dhhc4laniae, 366, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_statements_dafpns9 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_th2lqbo77pvsc gm_l2; /* info */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t5hdnnftkup6m gm_l7; /* b */
    gm_b16_8 gm_l8; /* internal */
    gm_b16_8 gm_l10; /* body */
    uint8_t gm_l11; /* changed */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t2s5j3n0guq0t gm_l14; /* d */
    uint8_t gm_l19; /* value */
    uint8_t gm_l22; /* unused */
    void * gm_l23; /* shown_c */
    gm_th2lqbo77pvsc gm_l24; /* kept */
    uint8_t gm_l25; /* impure */
    gm_th2lqbo77pvsc gm_l26; /* kept_text */
    gm_b16_8 gm_l27; /* k */
    uint8_t gm_l31; /* ok */
    gm_b16_8 gm_l32; /* k */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_t5hdnnftkup6m gm_l36; /* b */
    gm_b16_8 gm_l37; /* joined */
    GM_STACK_CHECK(&F, 3206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_tabb710d9mq2e gm_l45;
    uint8_t gm_l46;
    uint8_t gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tftb1odc739t5 gm_l51;
    {
        {
            gm_trqfvmjtt4sij t35;
            gm_f_list_glg30gi((void *)&t35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t35;
            gm_trqfvmjtt4sij t36;
            gm_f_list_e6ie1fq_2((void *)&t36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t36;
            void * t37 = (void *)((uint8_t *)&F.l4);
            void * t38 = gm_l0;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 128));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_tftb1odc739t5 t41 = gm_f_extend_e1j40m3(t37, t40);
            gm_l38 = t41;
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src3dhhc4laniae, 273, 5);
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_l5 = t43;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t44 = gm_l6;
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t46 = (t44 < t45);
                    if (!t46) {
                        goto gm_lb2;
                    }
                    {
                        void * t47 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t49 = gm_l6;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l7), (((uint8_t *)t47) + (uintptr_t)t49 * 24), 24);
                                }
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                gm_b16_8 t51;
                                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sfdbfrv46lihd));
                                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
                                uint8_t t52 = (gm_eq0iqiubfm7e(((uint8_t *)&t50), ((uint8_t *)&t51)));
                                if (t52) {
                                    goto gm_lb4;
                                }
                                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                gm_l8 = t53;
                                {
                                    void * t54 = gm_l0;
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t54) + 128));
                                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                    gm_f_lookup_var_3vpm3u6((void *)&gm_l40, t56, t57);
                                }
                                {
                                    {
                                        uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                                        if (t58) {
                                            goto gm_lb8;
                                        }
                                        gm_l39 = ((uint8_t)0);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    {
                                        gm_l39 = ((uint8_t)1);
                                        goto gm_lb7;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 277, 12);
                                }
gm_lb7:;
                                uint8_t t59 = gm_l39;
                                if (t59) {
                                    void * t60 = gm_l0;
                                    void * t61 = gm_l0;
                                    uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)t61) + 168));
                                    uint32_t r63; bool o64 = __builtin_add_overflow(t62, ((uint32_t)1ULL), &r63);
                                    if (o64) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 278, 13);
                                    uint32_t t65 = r63;
                                    GM_ST(uint32_t, (((uint8_t *)t60) + 168), t65);
                                    {
                                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                        void * t67 = gm_l0;
                                        uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)t67) + 168));
                                        gm_tommf4j9upomc t69;
                                        GM_ST(uint32_t, (uint8_t *)&t69 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t66);
                                        GM_ST(uint32_t, ((uint8_t *)&t69 + 16), t68);
                                        gm_f_format_5a05gmg((void *)&gm_l41, (void *)NULL, cx, NULL, &t69);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                    if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_src3dhhc4laniae, 279, 24);
                                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                                    gm_l8 = t72;
                                }
                                {
                                    void * t73 = (void *)((uint8_t *)&F.l3);
                                    uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
                                    uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 20));
                                    gm_b16_8 t76 = gm_l8;
                                    gm_t2s5j3n0guq0t t77;
                                    GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t74);
                                    GM_ST(uint32_t, ((uint8_t *)&t77 + 4), t75);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t76);
                                    gm_tftb1odc739t5 t78 = gm_f_push_f9bu01m(t73, &t77);
                                    gm_l42 = t78;
                                }
                                uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src3dhhc4laniae, 281, 9);
                                void * t80 = (void *)((uint8_t *)&F.l4);
                                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                gm_b16_8 t82 = gm_l8;
                                gm_b0_1 t83 = gm_f_set_var_pjsp5eq((void *)NULL, cx, NULL, t80, t81, t82);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb5;
                            }
gm_lb6:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 274, 5);
                        }
gm_lb5:;
                    }
gm_lb4:;
                    uint64_t t84 = gm_l6;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    uint64_t t87 = r85;
                    gm_l6 = t87;
                }
            }
gm_lb2:;
        }
        {
            void * t88 = gm_l0;
            gm_f_renamed_vars_fho5hvj((void *)&F.l9, (void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t89 = gm_l1;
            gm_l10 = t89;
            uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
            uint8_t t91 = (t90 > ((uint64_t)0ULL));
            gm_l11 = t91;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_l12 = t92;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t93 = gm_l13;
                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t95 = (t93 < t94);
                    if (!t95) {
                        goto gm_lb10;
                    }
                    {
                        void * t96 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t98 = gm_l13;
                        {
                            {
                                memmove(((uint8_t *)&gm_l14), (((uint8_t *)t96) + (uintptr_t)t98 * 24), 24);
                                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                                void * t100 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint32_t t102 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                uint64_t t103 = (uint64_t)t102;
                                uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                                uint64_t t105 = (uint64_t)t104;
                                uint64_t t106 = (uintptr_t)t103;
                                uint64_t t107 = (uintptr_t)t105;
                                if (t106 > t107 || t107 > (uintptr_t)t101) gm_trap_slice(t106, t107, t101, gm_src3dhhc4laniae, 288, 26);
                                uint64_t t108 = ((uintptr_t)t105) - (uintptr_t)t103;
                                gm_b16_8 t109;
                                GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t100 + (uintptr_t)t103 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108));
                                uint8_t t110 = (!gm_eq0iqiubfm7e(((uint8_t *)&t99), ((uint8_t *)&t109)));
                                if (t110) {
                                    gm_l11 = ((uint8_t)1);
                                }
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 287, 5);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t111 = gm_l13;
                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                    uint64_t t114 = r112;
                    gm_l13 = t114;
                }
            }
gm_lb10:;
            uint8_t t115 = gm_l11;
            if (t115) {
                gm_b16_8 t116 = gm_l1;
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                gm_b16_8 t121 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t116, t118, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t121;
            }
            void * t122 = gm_l0;
            gm_trqfvmjtt4sij t123;
            gm_f_replay_texts_3te9pi7((void *)&t123, (void *)NULL, cx, NULL, t122, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l15 = t123;
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_som0q53jqvjh3));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(4));
            gm_b16_8 t125 = gm_l10;
            gm_b16_8 t126;
            GM_ST(void *, (uint8_t *)&t126, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, ((uint8_t *)&F.l16 + 0), t124);
            GM_ST(gm_b16_8, ((uint8_t *)&F.l16 + 16), t125);
            GM_ST(gm_b16_8, ((uint8_t *)&F.l16 + 32), t126);
            void * t127 = gm_l0;
            gm_th2lqbo77pvsc t128;
            memset(&t128, 0, sizeof t128);
            gm_b16_8 t129 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
            gm_tfeab279c16rr t133 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l16));
            gm_td6svc7gs468a t134;
            GM_ST(uint8_t, (uint8_t *)&t134 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t134 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t134 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t134), t133);
            ((uint8_t *)&t134)[48] = 1;
            gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)gm_cd231ebi1kg));
            gm_toqvmi7jqorid t136;
            gm_f_compile_rj09gp7((void *)&t136, (void *)NULL, cx, NULL, t127, &t128, t130, t132, &t134, t135, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l17 = t136;
        }
        {
            memmove(((uint8_t *)&F.l18), ((uint8_t *)&F.l17), 64);
            gm_l19 = ((uint8_t)0);
            {
                {
                    uint8_t t137 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l17) + 24)) != 0);
                    if (t137) {
                        goto gm_lb16;
                    }
                    gm_l43 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    gm_l43 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb17:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 296, 8);
            }
gm_lb15:;
            uint8_t t138 = gm_l43;
            if (t138) {
            } else {
                uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
                if (t139) {
                    {
                        gm_b16_8 t140;
                        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sgvln0vb20p94));
                        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(16));
                        gm_b16_8 t141 = gm_l10;
                        gm_b16_8 t142;
                        GM_ST(void *, (uint8_t *)&t142, (void *)(gm_spun41oorb00o));
                        GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(29));
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 0), t140);
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 16), t141);
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 32), t142);
                    }
                    {
                        void * t143 = gm_l0;
                        gm_th2lqbo77pvsc t144;
                        memset(&t144, 0, sizeof t144);
                        gm_b16_8 t145 = gm_f_empty_slice_5nn7ph0();
                        gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                        gm_tfeab279c16rr t149 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l20));
                        gm_td6svc7gs468a t150;
                        GM_ST(uint8_t, (uint8_t *)&t150 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t150 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t150 + 52, 0);
                        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t150), t149);
                        ((uint8_t *)&t150)[48] = 1;
                        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)gm_cd231ebi1kg));
                        gm_toqvmi7jqorid t152;
                        gm_f_compile_rj09gp7((void *)&t152, (void *)NULL, cx, NULL, t143, &t144, t146, t148, &t150, t151, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l21 = t152;
                    }
                    {
                        {
                            uint8_t t153 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l21) + 24)) != 0);
                            if (t153) {
                                goto gm_lb19;
                            }
                            gm_l44 = ((uint8_t)0);
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            gm_l44 = ((uint8_t)1);
                            goto gm_lb18;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 301, 12);
                    }
gm_lb18:;
                    uint8_t t154 = gm_l44;
                    if (t154) {
                        memmove(((uint8_t *)&F.l18), ((uint8_t *)&F.l21), 64);
                        gm_l19 = ((uint8_t)1);
                        void * t155 = (void *)((uint8_t *)&F.l17);
                        gm_b0_1 t156 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        gm_l22 = ((uint8_t)0);
                        void * t157 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                        uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)t157) + 8));
                        uint8_t t159 = (t158 > ((uint64_t)0ULL));
                        if (t159) {
                            void * t160 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                            void * t161 = GM_LD(void *, ((uint8_t *)t160));
                            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)t160) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t162)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t162, gm_src3dhhc4laniae, 307, 71);
                            gm_b16_8 t163 = GM_LD(gm_b16_8, ((((uint8_t *)t161) + (uintptr_t)((uint64_t)0ULL) * 184) + 8));
                            gm_b16_8 t164;
                            GM_ST(void *, (uint8_t *)&t164, (void *)(gm_sshi9ffc6nkc4));
                            GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(9));
                            uint8_t t165 = gm_f_contains_gkki4kq(t163, t164);
                            gm_l22 = t165;
                        }
                        uint8_t t166 = gm_l22;
                        if (t166) {
                            void * t167 = (void *)((uint8_t *)&F.l21);
                            gm_l23 = t167;
                        } else {
                            void * t168 = (void *)((uint8_t *)&F.l17);
                            gm_l23 = t168;
                        }
                        void * t169 = gm_l23;
                        void * t170 = gm_l0;
                        gm_t9qkh0rsooc8d t171 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t170) + 2));
                        gm_b16_8 t172 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t169, t171);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t173 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t172);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t174 = (void *)((uint8_t *)&F.l21);
                        gm_b0_1 t175 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t174);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t176 = (void *)((uint8_t *)&F.l17);
                        gm_b0_1 t177 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t176);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t178 = (void *)((uint8_t *)&F.l15);
                        gm_f_deinit_gvn47sj(t178);
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                } else {
                    void * t179 = (void *)((uint8_t *)&F.l17);
                    void * t180 = gm_l0;
                    gm_t9qkh0rsooc8d t181 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t180) + 2));
                    gm_b16_8 t182 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t179, t181);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b0_1 t183 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t184 = (void *)((uint8_t *)&F.l17);
                    gm_b0_1 t185 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t184);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t186 = (void *)((uint8_t *)&F.l15);
                    gm_f_deinit_gvn47sj(t186);
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
            }
        }
        {
            void * t187 = (void *)((uint8_t *)&F.l15);
            gm_f_deinit_gvn47sj(t187);
            void * t188 = (void *)((uint8_t *)&F.l18);
            void * t189 = gm_l0;
            gm_t9qkh0rsooc8d t190 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t189) + 2));
            gm_b16_8 t191 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t188, t190);
            if (gm_unw.kind) {
                goto gm_ld34_1;
            }
            gm_b0_1 t192 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t191);
            if (gm_unw.kind) {
                goto gm_ld34_1;
            }
            memset(&gm_l24, 0, sizeof gm_l24);
            uint8_t t193 = gm_l19;
            uint8_t t194 = (uint8_t)!t193;
            if (t194) {
                gm_b16_8 t195 = gm_l10;
                GM_ST(uint8_t, (uint8_t *)&gm_l24 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l24 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l24 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l24), t195);
                ((uint8_t *)&gm_l24)[16] = 1;
            } else {
                gm_b16_8 t196 = gm_l10;
                uint8_t t197 = gm_f_may_mutate_uj4e7jp((void *)NULL, cx, NULL, t196);
                if (gm_unw.kind) {
                    goto gm_ld34_1;
                }
                if (t197) {
                    {
                        gm_b16_8 t198 = gm_l10;
                        gm_tnpifv20irnph t199;
                        GM_ST(gm_b16_8, ((uint8_t *)&t199 + 0), t198);
                        gm_f_format_uq5dnfv((void *)&gm_l45, (void *)NULL, cx, NULL, t199);
                        if (gm_unw.kind) {
                            goto gm_ld34_1;
                        }
                    }
                    uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                    if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src3dhhc4laniae, 330, 16);
                    {
                        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                        GM_ST(uint8_t, (uint8_t *)&gm_l24 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l24 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l24 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l24), t202);
                        ((uint8_t *)&gm_l24)[16] = 1;
                    }
                }
            }
            gm_l25 = ((uint8_t)0);
            memset(&gm_l26, 0, sizeof gm_l26);
            {
                {
                    uint8_t t203 = (uint8_t)((((uint8_t *)&gm_l24))[16] != 0);
                    if (!t203) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                    gm_l27 = t204;
                    {
                        void * t205 = gm_l0;
                        gm_trqfvmjtt4sij t206;
                        gm_f_replay_texts_3te9pi7((void *)&t206, (void *)NULL, cx, NULL, t205, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_ld34_1;
                        }
                        F.l28 = t206;
                    }
                    {
                        gm_b16_8 t207;
                        GM_ST(void *, (uint8_t *)&t207, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(4));
                        gm_b16_8 t208 = gm_l27;
                        gm_b16_8 t209;
                        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_so514oj5nbhcu));
                        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(1));
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l29 + 0), t207);
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l29 + 16), t208);
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l29 + 32), t209);
                    }
                    {
                        void * t210 = gm_l0;
                        gm_th2lqbo77pvsc t211;
                        memset(&t211, 0, sizeof t211);
                        gm_b16_8 t212 = gm_f_empty_slice_5nn7ph0();
                        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                        gm_tfeab279c16rr t216 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l29));
                        gm_td6svc7gs468a t217;
                        GM_ST(uint8_t, (uint8_t *)&t217 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t217 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t217 + 52, 0);
                        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t217), t216);
                        ((uint8_t *)&t217)[48] = 1;
                        gm_b16_8 t218;
                        GM_ST(void *, (uint8_t *)&t218, (void *)(gm_s1qrqfh656g4q));
                        GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(5));
                        gm_toqvmi7jqorid t219;
                        gm_f_compile_rj09gp7((void *)&t219, (void *)NULL, cx, NULL, t210, &t211, t213, t215, &t217, t218, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_ld34_1;
                        }
                        F.l30 = t219;
                    }
                    {
                        {
                            uint8_t t220 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l30) + 24)) != 0);
                            if (t220) {
                                goto gm_lb24;
                            }
                            gm_l46 = ((uint8_t)1);
                            goto gm_lb23;
                        }
gm_lb24:;
                        {
                            gm_l46 = ((uint8_t)0);
                            goto gm_lb23;
                        }
gm_lb25:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 338, 18);
                    }
gm_lb23:;
                    uint8_t t221 = gm_l46;
                    gm_l25 = t221;
                    void * t222 = (void *)((uint8_t *)&F.l30);
                    gm_b0_1 t223 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t222);
                    if (gm_unw.kind) {
                        goto gm_ld34_1;
                    }
                    void * t224 = (void *)((uint8_t *)&F.l28);
                    gm_f_deinit_gvn47sj(t224);
                    uint8_t t225 = gm_l25;
                    uint8_t t226 = (uint8_t)!t225;
                    if (t226) {
                        gm_l47 = ((uint8_t)1);
                    } else {
                        uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
                        uint8_t t228 = (t227 > ((uint64_t)0ULL));
                        gm_l47 = t228;
                    }
                    uint8_t t229 = gm_l47;
                    if (t229) {
                        gm_b16_8 t230 = gm_l27;
                        GM_ST(uint8_t, (uint8_t *)&gm_l26 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l26 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l26 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l26), t230);
                        ((uint8_t *)&gm_l26)[16] = 1;
                    }
                    goto gm_lb21;
                }
gm_lb22:;
            }
gm_lb21:;
        }
        {
            void * t231 = (void *)((uint8_t *)&F.l18);
            void * t232 = gm_l0;
            gm_t9qkh0rsooc8d t233 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t232) + 1));
            void * t234 = gm_l0;
            uint64_t t235 = GM_LD(uint64_t, ((((uint8_t *)t234) + 88) + 8));
            uint8_t t236 = (t235 > ((uint64_t)0ULL));
            uint8_t t237 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t231, t233, t236);
            if (gm_unw.kind) {
                goto gm_ld34_1;
            }
            gm_l31 = t237;
            uint8_t t238 = gm_l31;
            uint8_t t239 = (uint8_t)!t238;
            if (t239) {
                F.ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld34_1;
            }
            {
                {
                    uint8_t t240 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
                    if (!t240) {
                        goto gm_lb27;
                    }
                    gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                    gm_l32 = t241;
                    uint8_t t242 = gm_l25;
                    if (t242) {
                        {
                            gm_trqfvmjtt4sij t243;
                            gm_f_list_epvducg((void *)&t243, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ld34_1;
                            }
                            F.l33 = t243;
                        }
                        gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_l34 = t244;
                        gm_l35 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t245 = gm_l35;
                                uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                uint8_t t247 = (t245 < t246);
                                if (!t247) {
                                    goto gm_lb28;
                                }
                                {
                                    void * t248 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                    uint64_t t250 = gm_l35;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l36), (((uint8_t *)t248) + (uintptr_t)t250 * 24), 24);
                                            void * t251 = (void *)((uint8_t *)&F.l33);
                                            gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l36));
                                            gm_tftb1odc739t5 t253 = gm_f_push_3ef746b(t251, t252);
                                            gm_l48 = t253;
                                            uint8_t t254 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                            if (!t254) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src3dhhc4laniae, 348, 38);
                                            goto gm_lb31;
                                        }
gm_lb32:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 348, 13);
                                    }
gm_lb31:;
                                }
gm_lb30:;
                                uint64_t t255 = gm_l35;
                                uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                uint64_t t258 = r256;
                                gm_l35 = t258;
                            }
                        }
gm_lb28:;
                        {
                            gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
                            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
                            gm_b16_8 t261;
                            GM_ST(void *, (uint8_t *)&t261, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(2));
                            gm_f_join_sfknljb((void *)&gm_l49, (void *)NULL, cx, NULL, t260, t261);
                            if (gm_unw.kind) {
                                goto gm_ld34_1;
                            }
                        }
                        uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                        if (!t262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_src3dhhc4laniae, 349, 23);
                        gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                        gm_l37 = t263;
                        gm_b16_8 t264 = gm_l37;
                        if (gm_pf454qfrpt6r(cx, gm_smrc1u3r4i5hd, 6, gm_src3dhhc4laniae, 350, 13)) {
                            goto gm_ld34_1;
                        }
                        if (gm_pfu8dq5bikbh(cx, &gm_tybihgc69jv3, ((uint8_t *)&t264), gm_src3dhhc4laniae, 350, 13)) {
                            goto gm_ld34_1;
                        }
                        if (gm_pf454qfrpt6r(cx, gm_s68c5jkfvg5dd, 85, gm_src3dhhc4laniae, 350, 13)) {
                            goto gm_ld34_1;
                        }
                    }
                    {
                        void * t265 = gm_l0;
                        void * t266 = (void *)(((uint8_t *)t265) + 88);
                        gm_b16_8 t267 = gm_l1;
                        gm_f_dupe_g9iqpvn((void *)&gm_l50, (void *)NULL, cx, NULL, t267, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmasfkh2un)));
                        if (gm_unw.kind) {
                            goto gm_ld34_1;
                        }
                        uint8_t t268 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                        if (!t268) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_src3dhhc4laniae, 352, 32);
                        gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                        gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                        gm_b16_8 t271 = gm_l32;
                        uint8_t t272 = gm_l25;
                        gm_tk9n3vlila9qg t273;
                        GM_ST(uint8_t, (uint8_t *)&t273 + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&t273 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t273 + 36, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t273 + 0), t270);
                        GM_ST(gm_b16_8, ((uint8_t *)&t273 + 16), t271);
                        GM_ST(uint8_t, ((uint8_t *)&t273 + 32), t272);
                        gm_tftb1odc739t5 t274 = gm_f_push_ej78v89(t266, &t273);
                        gm_l51 = t274;
                    }
                    uint8_t t275 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (!t275) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src3dhhc4laniae, 352, 9);
                    goto gm_lb26;
                }
gm_lb27:;
            }
gm_lb26:;
        }
        {
            void * t276 = gm_l0;
            memmove((((uint8_t *)t276) + 128), ((uint8_t *)&F.l4), 40);
            F.ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld34_1;
        }
    }
    {
        void * t277 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t278 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t277);
    }
    goto gm_ls279;
gm_ld34_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t280 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t281 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t280);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls279:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_submit_pah0u77 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* items */
    gm_ti5ramhglnvdt gm_l4; /* local */
    gm_th2lqbo77pvsc gm_l5; /* s */
    uint8_t gm_l6; /* all */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* msg */
    gm_th2lqbo77pvsc gm_l11; /* s */
    gm_th2lqbo77pvsc gm_l12; /* s */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    {
        {
            {
                {
                    uint8_t t23 = (uint8_t)((((uint8_t *)a2))[16] != 0);
                    if (t23) {
                        goto gm_lb4;
                    }
                    gm_l13 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 8);
            }
gm_lb3:;
            uint8_t t24 = gm_l13;
            if (!t24) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t25 = (uint8_t)(((((uint8_t *)a2) + 24))[24] != 0);
                    if (t25) {
                        goto gm_lb7;
                    }
                    gm_l14 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l14 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 28);
            }
gm_lb6:;
            uint8_t t26 = gm_l14;
            if (!t26) {
                goto gm_lb2;
            }
            gm_b16_8 t27 = gm_l1;
            void * t28 = gm_l0;
            gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t28) + 2));
            gm_b16_8 t30 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t27, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t31 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t32 = (uint8_t)((((uint8_t *)a2))[16] != 0);
            if (!t32) {
                goto gm_lb10;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_l3 = t33;
            {
                memset(&gm_l4, 0, sizeof gm_l4);
            }
            {
                {
                    uint8_t t34 = (uint8_t)(((((uint8_t *)a2) + 24))[24] != 0);
                    if (!t34) {
                        goto gm_lb12;
                    }
                    memmove(((uint8_t *)&gm_l5), (((uint8_t *)a2) + 24), 24);
                    gm_l6 = ((uint8_t)1);
                    gm_b16_8 t35 = gm_l3;
                    gm_l7 = t35;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t36 = gm_l8;
                            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t38 = (t36 < t37);
                            if (!t38) {
                                goto gm_lb13;
                            }
                            {
                                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t41 = gm_l8;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t39) + (uintptr_t)t41 * 40), 40);
                                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 32));
                                        uint8_t t43 = (uint8_t)!t42;
                                        if (t43) {
                                            gm_l6 = ((uint8_t)0);
                                        }
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 222, 13);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t44 = gm_l8;
                            uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                            uint64_t t47 = r45;
                            gm_l8 = t47;
                        }
                    }
gm_lb13:;
                    uint8_t t48 = gm_l6;
                    if (t48) {
                        gm_th2lqbo77pvsc t49 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l5));
                        GM_ST(uint8_t, (uint8_t *)&gm_l4 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l4 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l4 + 28, 0);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4), t49);
                        ((uint8_t *)&gm_l4)[24] = 1;
                    }
                    goto gm_lb11;
                }
gm_lb12:;
            }
gm_lb11:;
            {
                void * t50 = gm_l0;
                gm_b16_8 t51 = gm_l1;
                gm_b16_8 t52 = gm_l3;
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_f_try_decls_23vcohd((void *)&gm_l15, (void *)NULL, cx, NULL, t50, t51, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
            if (t54) {
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                gm_l10 = t55;
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l4))[24] != 0);
                    if (!t56) {
                        goto gm_lb19;
                    }
                    {
                        memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l4), 24);
                    }
                    {
                        void * t57 = gm_l0;
                        gm_b16_8 t58 = gm_l1;
                        uint8_t t59 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t57, t58, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb18;
                }
gm_lb19:;
                gm_b16_8 t60 = gm_l10;
                gm_b0_1 t61 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb18:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb10:;
    }
gm_lb9:;
    {
        {
            uint8_t t62 = (uint8_t)(((((uint8_t *)a2) + 24))[24] != 0);
            if (!t62) {
                goto gm_lb21;
            }
            {
                memmove(((uint8_t *)&gm_l12), (((uint8_t *)a2) + 24), 24);
            }
            {
                void * t63 = gm_l0;
                gm_b16_8 t64 = gm_l1;
                uint8_t t65 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t63, t64, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb20;
        }
gm_lb21:;
    }
gm_lb20:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_newlines_9rj3i73 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 190u);
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
            void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t10 = gm_l1;
            uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
            if (o12) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 23);
            uint64_t t13 = r11;
            if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_src3dhhc4laniae, 503, 21);
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)t13 * 1));
            gm_tqs5p4n2tq19v t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)10ULL));
            GM_ST(uint8_t, ((uint8_t *)&t15 + 1), ((uint8_t)13ULL));
            uint8_t t16 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t15) + 0, ((uint8_t *)&t14))) t16 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t15) + 1, ((uint8_t *)&t14))) t16 = 1;
            uint8_t t17 = (uint8_t)(t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                uint64_t t18 = gm_l1;
                uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 48);
                uint64_t t21 = r19;
                gm_l1 = t21;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t24 = gm_l1;
    uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t26 = (uintptr_t)t24;
    if (t25 > t26 || t26 > (uintptr_t)t23) gm_trap_slice(t25, t26, t23, gm_src3dhhc4laniae, 504, 12);
    uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_try_decls_23vcohd F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l2; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t6j6smj7qoctu gm_l15; /* ch */
    uint8_t gm_l16; /* replaced */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* n */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    GM_STACK_CHECK(&F, 1439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l26;
    uint8_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    {
        {
            gm_trqfvmjtt4sij t37;
            gm_f_list_epvducg((void *)&t37, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t37;
        }
        gm_b16_8 t38 = gm_l2;
        gm_l4 = t38;
        gm_l5 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t39 = gm_l5;
                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t41 = (t39 < t40);
                if (!t41) {
                    goto gm_lb2;
                }
                {
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t44 = gm_l5;
                    {
                        {
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t42) + (uintptr_t)t44 * 40), 40);
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                            gm_l7 = t45;
                            gm_l8 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t46 = gm_l8;
                                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint8_t t48 = (t46 < t47);
                                    if (!t48) {
                                        goto gm_lb7;
                                    }
                                    {
                                        void * t49 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint64_t t51 = gm_l8;
                                        {
                                            {
                                                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t49) + (uintptr_t)t51 * 16));
                                                gm_l9 = t52;
                                                gm_b16_8 t53 = gm_l9;
                                                gm_b16_8 t54;
                                                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sog4rksrmsqd2));
                                                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(4));
                                                uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                                                if (t55) {
                                                    gm_b16_8 t56;
                                                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sn1sq59pu45vv));
                                                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(38));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t56);
                                                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                                    goto gm_fdone;
                                                }
                                                void * t57 = (void *)((uint8_t *)&F.l3);
                                                gm_b16_8 t58 = gm_l9;
                                                gm_tftb1odc739t5 t59 = gm_f_push_3ef746b(t57, t58);
                                                gm_l26 = t59;
                                                uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src3dhhc4laniae, 244, 13);
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 242, 9);
                                        }
gm_lb10:;
                                    }
gm_lb9:;
                                    uint64_t t61 = gm_l8;
                                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                    uint64_t t64 = r62;
                                    gm_l8 = t64;
                                }
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 241, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t65 = gm_l5;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                uint64_t t68 = r66;
                gm_l5 = t68;
            }
        }
gm_lb2:;
        {
            void * t69 = gm_l0;
            gm_trqfvmjtt4sij t70;
            gm_f_replay_texts_3te9pi7((void *)&t70, (void *)NULL, cx, NULL, t69, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t70;
        }
        {
            void * t71 = gm_l0;
            gm_b16_8 t72 = gm_l1;
            gm_th2lqbo77pvsc t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t73), t72);
            ((uint8_t *)&t73)[16] = 1;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_td6svc7gs468a t78;
            memset(&t78, 0, sizeof t78);
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)gm_cd231ebi1kg));
            gm_toqvmi7jqorid t80;
            gm_f_compile_rj09gp7((void *)&t80, (void *)NULL, cx, NULL, t71, &t73, t75, t77, &t78, t79, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l11 = t80;
        }
        void * t81 = (void *)((uint8_t *)&F.l10);
        gm_f_deinit_gvn47sj(t81);
        {
            {
                uint8_t t82 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l11) + 24)) != 0);
                if (t82) {
                    goto gm_lb13;
                }
                gm_l27 = ((uint8_t)1);
                goto gm_lb12;
            }
gm_lb13:;
            {
                gm_l27 = ((uint8_t)0);
                goto gm_lb12;
            }
gm_lb14:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 251, 8);
        }
gm_lb12:;
        uint8_t t83 = gm_l27;
        if (t83) {
            void * t84 = (void *)((uint8_t *)&F.l11);
            void * t85 = gm_l0;
            gm_t9qkh0rsooc8d t86 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t85) + 2));
            gm_b16_8 t87 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t84, t86);
            if (gm_unw.kind) {
                goto gm_ld36_1;
            }
            GM_ST(uint8_t, (uint8_t *)&F.ret + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&F.ret), t87);
            ((uint8_t *)&F.ret)[16] = 1;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld36_1;
        }
        void * t88 = (void *)((uint8_t *)&F.l11);
        void * t89 = gm_l0;
        gm_t9qkh0rsooc8d t90 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t89) + 2));
        gm_b16_8 t91 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t88, t90);
        if (gm_unw.kind) {
            goto gm_ld36_1;
        }
        gm_b0_1 t92 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t91);
        if (gm_unw.kind) {
            goto gm_ld36_1;
        }
        {
            gm_trqfvmjtt4sij t93;
            gm_f_list_1t36jvm((void *)&t93, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld36_1;
            }
            F.l12 = t93;
        }
        void * t94 = gm_l0;
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)t94) + 48));
        gm_l13 = t95;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t96 = gm_l14;
                uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t98 = (t96 < t97);
                if (!t98) {
                    goto gm_lb15;
                }
                {
                    void * t99 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint64_t t101 = gm_l14;
                    {
                        {
                            memmove(((uint8_t *)&gm_l15), (((uint8_t *)t99) + (uintptr_t)t101 * 32), 32);
                            gm_l16 = ((uint8_t)0);
                            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                            gm_l17 = t102;
                            gm_l18 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t103 = gm_l18;
                                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                    uint8_t t105 = (t103 < t104);
                                    if (!t105) {
                                        goto gm_lb20;
                                    }
                                    {
                                        void * t106 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                        uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint64_t t108 = gm_l18;
                                        {
                                            {
                                                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)t106) + (uintptr_t)t108 * 16));
                                                gm_l19 = t109;
                                                gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                                gm_l20 = t110;
                                                gm_l21 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t111 = gm_l21;
                                                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                        uint8_t t113 = (t111 < t112);
                                                        if (!t113) {
                                                            goto gm_lb25;
                                                        }
                                                        {
                                                            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                            uint64_t t116 = gm_l21;
                                                            {
                                                                {
                                                                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)t114) + (uintptr_t)t116 * 16));
                                                                    gm_l22 = t117;
                                                                    gm_b16_8 t118 = gm_l19;
                                                                    gm_b16_8 t119 = gm_l22;
                                                                    uint8_t t120 = (gm_eq0iqiubfm7e(((uint8_t *)&t118), ((uint8_t *)&t119)));
                                                                    if (t120) {
                                                                        gm_l16 = ((uint8_t)1);
                                                                    }
                                                                    goto gm_lb28;
                                                                }
gm_lb29:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 257, 13);
                                                            }
gm_lb28:;
                                                        }
gm_lb27:;
                                                        uint64_t t121 = gm_l21;
                                                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                                        uint64_t t124 = r122;
                                                        gm_l21 = t124;
                                                    }
                                                }
gm_lb25:;
                                                goto gm_lb23;
                                            }
gm_lb24:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 256, 9);
                                        }
gm_lb23:;
                                    }
gm_lb22:;
                                    uint64_t t125 = gm_l18;
                                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                                    uint64_t t128 = r126;
                                    gm_l18 = t128;
                                }
                            }
gm_lb20:;
                            uint8_t t129 = gm_l16;
                            uint8_t t130 = (uint8_t)!t129;
                            if (t130) {
                                void * t131 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t132 = gm_f_push_52bt1f1(t131, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l15)));
                                gm_l28 = t132;
                                uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src3dhhc4laniae, 261, 27);
                            }
                            goto gm_lb18;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 254, 5);
                    }
gm_lb18:;
                }
gm_lb17:;
                uint64_t t134 = gm_l14;
                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                uint64_t t137 = r135;
                gm_l14 = t137;
            }
        }
gm_lb15:;
        gm_b16_8 t138 = gm_l2;
        gm_l23 = t138;
        gm_l24 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t139 = gm_l24;
                uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                uint8_t t141 = (t139 < t140);
                if (!t141) {
                    goto gm_lb30;
                }
                {
                    void * t142 = GM_LD(void *, ((uint8_t *)&gm_l23));
                    uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                    uint64_t t144 = gm_l24;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l25), (((uint8_t *)t142) + (uintptr_t)t144 * 40), 40);
                            }
                            {
                                void * t145 = (void *)((uint8_t *)&F.l12);
                                gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 16));
                                gm_t6j6smj7qoctu t148;
                                GM_ST(gm_b16_8, ((uint8_t *)&t148 + 0), t146);
                                GM_ST(gm_b16_8, ((uint8_t *)&t148 + 16), t147);
                                gm_tftb1odc739t5 t149 = gm_f_push_52bt1f1(t145, &t148);
                                gm_l29 = t149;
                            }
                            uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                            if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src3dhhc4laniae, 263, 22);
                            goto gm_lb33;
                        }
gm_lb34:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 263, 5);
                    }
gm_lb33:;
                }
gm_lb32:;
                uint64_t t151 = gm_l24;
                uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                uint64_t t154 = r152;
                gm_l24 = t154;
            }
        }
gm_lb30:;
        {
            void * t155 = gm_l0;
            memmove((((uint8_t *)t155) + 48), ((uint8_t *)&F.l12), 40);
        }
        {
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld36_1;
        }
    }
    {
        void * t156 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t157 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t156);
    }
    goto gm_ls158;
gm_ld36_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t159 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t160 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t159);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls158:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_type_of_b1a5sco F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* source_text */
    gm_b16_8 gm_l4; /* expr */
    GM_STACK_CHECK(&F, 995u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    {
        {
            void * t10 = gm_l0;
            gm_trqfvmjtt4sij t11;
            gm_f_replay_texts_3te9pi7((void *)&t11, (void *)NULL, cx, NULL, t10, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t11;
        }
        {
            void * t12 = gm_l0;
            gm_f_renamed_vars_fho5hvj((void *)&F.l3, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_ld9_1;
            }
        }
        gm_b16_8 t13 = gm_l1;
        gm_l4 = t13;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint8_t t15 = (t14 > ((uint64_t)0ULL));
        if (t15) {
            gm_b16_8 t16 = gm_l1;
            gm_b16_8 t17 = gm_f_empty_slice_q5bjns8();
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_b16_8 t21 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t16, t18, t20);
            if (gm_unw.kind) {
                goto gm_ld9_1;
            }
            gm_l4 = t21;
        }
        {
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_skkiv5ia4t4au));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(38));
            gm_b16_8 t23 = gm_l4;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_squacltecdsom));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, ((uint8_t *)&F.l5 + 0), t22);
            GM_ST(gm_b16_8, ((uint8_t *)&F.l5 + 16), t23);
            GM_ST(gm_b16_8, ((uint8_t *)&F.l5 + 32), t24);
        }
        {
            void * t25 = gm_l0;
            gm_th2lqbo77pvsc t26;
            memset(&t26, 0, sizeof t26);
            gm_b16_8 t27 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_tfeab279c16rr t31 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l5));
            gm_td6svc7gs468a t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[48] = 1;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)gm_cd231ebi1kg));
            gm_toqvmi7jqorid t34;
            gm_f_compile_rj09gp7((void *)&t34, (void *)NULL, cx, NULL, t25, &t26, t28, t30, &t32, t33, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ld9_1;
            }
            F.l6 = t34;
        }
        {
            {
                uint8_t t35 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l6) + 24)) != 0);
                if (t35) {
                    goto gm_lb3;
                }
                gm_l9 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb3:;
            {
                gm_l9 = ((uint8_t)0);
                goto gm_lb2;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 378, 8);
        }
gm_lb2:;
        uint8_t t36 = gm_l9;
        if (t36) {
            {
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sojb2hhp0ln7r));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(30));
                gm_b16_8 t38 = gm_l4;
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s055t7hpo88bt));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l7 + 0), t37);
                GM_ST(gm_b16_8, ((uint8_t *)&F.l7 + 16), t38);
                GM_ST(gm_b16_8, ((uint8_t *)&F.l7 + 32), t39);
            }
            {
                void * t40 = gm_l0;
                gm_th2lqbo77pvsc t41;
                memset(&t41, 0, sizeof t41);
                gm_b16_8 t42 = gm_f_empty_slice_5nn7ph0();
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_tfeab279c16rr t46 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l7));
                gm_td6svc7gs468a t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 52, 0);
                GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t47), t46);
                ((uint8_t *)&t47)[48] = 1;
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)gm_cd231ebi1kg));
                gm_toqvmi7jqorid t49;
                gm_f_compile_rj09gp7((void *)&t49, (void *)NULL, cx, NULL, t40, &t41, t43, t45, &t47, t48, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_ld9_1;
                }
                F.l8 = t49;
            }
            {
                {
                    uint8_t t50 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 24)) != 0);
                    if (t50) {
                        goto gm_lb6;
                    }
                    gm_l10 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    gm_l10 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 381, 12);
            }
gm_lb5:;
            uint8_t t51 = gm_l10;
            if (t51) {
                void * t52 = (void *)((uint8_t *)&F.l6);
                void * t53 = gm_l0;
                gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t53) + 2));
                gm_b16_8 t55 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t52, t54);
                if (gm_unw.kind) {
                    goto gm_ld9_1;
                }
                gm_b0_1 t56 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t55);
                if (gm_unw.kind) {
                    goto gm_ld9_1;
                }
                void * t57 = (void *)((uint8_t *)&F.l6);
                gm_b0_1 t58 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t57);
                if (gm_unw.kind) {
                    goto gm_ld9_1;
                }
                void * t59 = (void *)((uint8_t *)&F.l8);
                gm_b0_1 t60 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t59);
                if (gm_unw.kind) {
                    goto gm_ld9_1;
                }
                F.ret = GM_ZERO(gm_b0_1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld9_1;
            }
            void * t61 = (void *)((uint8_t *)&F.l6);
            gm_b0_1 t62 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_ld9_1;
            }
            {
                memmove(((uint8_t *)&F.l6), ((uint8_t *)&F.l8), 64);
            }
        }
        void * t63 = (void *)((uint8_t *)&F.l6);
        void * t64 = gm_l0;
        gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t64) + 1));
        void * t66 = gm_l0;
        uint64_t t67 = GM_LD(uint64_t, ((((uint8_t *)t66) + 88) + 8));
        uint8_t t68 = (t67 > ((uint64_t)0ULL));
        uint8_t t69 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t63, t65, t68);
        if (gm_unw.kind) {
            goto gm_ld9_1;
        }
        void * t70 = (void *)((uint8_t *)&F.l6);
        gm_b0_1 t71 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t70);
        if (gm_unw.kind) {
            goto gm_ld9_1;
        }
    }
    {
        void * t72 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t72);
    }
    goto gm_ls73;
gm_ld9_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t74 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t74);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls73:;
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

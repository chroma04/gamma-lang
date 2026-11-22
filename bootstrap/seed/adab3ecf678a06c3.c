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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
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
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
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
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { gm_b16_8 f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tdu0qimd4ae4c;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { uint64_t f0; gm_tbgd14esf0jvi f1; } gm_tdlq2ugehok8g;
_Static_assert(sizeof(gm_tdu0qimd4ae4c) == 32 && _Alignof(gm_tdu0qimd4ae4c) == 8 && offsetof(gm_tdu0qimd4ae4c, f0) == 0 && offsetof(gm_tdu0qimd4ae4c, f1) == 16 && offsetof(gm_tdu0qimd4ae4c, f2) == 20, "layout of `PatBind`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_toceffb697p0u f2; } gm_tqs4ilbobv1qq;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tdlq2ugehok8g) == 40 && _Alignof(gm_tdlq2ugehok8g) == 8 && offsetof(gm_tdlq2ugehok8g, f0) == 0 && offsetof(gm_tdlq2ugehok8g, f1) == 8, "layout of `(i: usize, x: Edit)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqs4ilbobv1qq) == 56 && _Alignof(gm_tqs4ilbobv1qq) == 8 && offsetof(gm_tqs4ilbobv1qq, f0) == 0 && offsetof(gm_tqs4ilbobv1qq, f1) == 24 && offsetof(gm_tqs4ilbobv1qq, f2) == 40, "layout of `MatchReport`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_arm_pattern_rftjhhb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* first_binds */
    gm_trqfvmjtt4sij l4; /* alts */
    gm_trqfvmjtt4sij l9; /* binds */
    gm_trqfvmjtt4sij l15; /* map */
    void * ret;
};
struct gm_fr_binding_cond_v86td9t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* binds */
    gm_tlr9dp305htm7 ret;
};
struct gm_fr_binds_unknown_name_7m00m7a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_c_union_match_2tgsktr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_arms_65v4isv {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* pats */
    gm_trqfvmjtt4sij l4; /* guarded */
    gm_tqs4ilbobv1qq ret;
};
struct gm_fr_check_deferred_match_742e2u5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_contains_wildcard_jgbf94d {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_l26i0t5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tcmn71c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_expr_to_pattern_rchr7ig {
    gm_frame h;
    gm_to361lgknsu69 l1; /* kind */
    gm_trqfvmjtt4sij l4; /* pargs */
    gm_tc3oht4ksth9d l9; /* v */
    gm_t0d02142k4tu8 l10; /* np */
    gm_tc3oht4ksth9d l12; /* v */
    gm_t0d02142k4tu8 l13; /* np */
    gm_t0d02142k4tu8 l16; /* inner */
    gm_trqfvmjtt4sij l18; /* pargs */
    gm_t0d02142k4tu8 l23; /* np */
    gm_toqf51949ppls ret;
};
struct gm_fr_fn_ho0p9pu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_d277g96 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t8lugsu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tb06a6e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_patterns_mi6p7hb {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* arms */
    gm_toqf51949ppls l12; /* p */
    gm_t0d02142k4tu8 l13; /* pp */
    gm_trqfvmjtt4sij l14; /* binds */
    void * ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_pattern_operand_p51tban {
    gm_frame h;
    gm_t0d02142k4tu8 l3; /* pat */
    gm_toqf51949ppls ret;
};
struct gm_fr_is_pattern_test_kdd7cfh {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* binds */
    gm_trqfvmjtt4sij l11; /* arms */
    void * ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_br4lm90 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_match_expr_npcoo60 {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* arms */
    gm_t79rbdv4rafe5 l18; /* b */
    gm_trqfvmjtt4sij l20; /* arm_spans */
    gm_trqfvmjtt4sij l26; /* branches */
    void * ret;
};
struct gm_fr_nested_pattern_oiclpgf {
    gm_frame h;
    gm_toqf51949ppls ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_omits_payload_e33pdef {
    gm_frame h;
    gm_tckdobunaulee l17; /* v */
    uint8_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2uqiirs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ltrd0kq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hm23q6l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_i2ujhou {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remap_binds_kao301q {
    gm_frame h;
    gm_b16_8 l1; /* map */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_match_d77kbfe {
    gm_frame h;
    gm_tqs4ilbobv1qq l1; /* report */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_arm_pattern_rftjhhb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_binds_unknown_name_7m00m7a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_bound_n051ued(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_check_arms_65v4isv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_wildcard_jgbf94d(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l26i0t5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_expr_to_pattern_rchr7ig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_d277g96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t8lugsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_c_union_p1r4eve(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_is_pattern_test_kdd7cfh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_nested_pattern_oiclpgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_f7qroea(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_omits_payload_e33pdef(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2uqiirs(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ltrd0kq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm23q6l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_i2ujhou(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remap_binds_kao301q(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_match_d77kbfe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_swap_remove_lfi9t15(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_k9foool24ve[40] __attribute__((aligned(8)));
extern uint8_t gm_kdhojj6r45o[40] __attribute__((aligned(8)));
extern uint8_t gm_kns860jaqph[40] __attribute__((aligned(8)));
extern uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
extern const uint8_t gm_s3ceboq671qr5[39];
extern const uint8_t gm_s5p756i54h2sb[52];
extern const uint8_t gm_s67h6d8kdg7qc[167];
extern const uint8_t gm_s8fq66mhsgcib[104];
extern const uint8_t gm_sa8p4rum5g6ig[116];
extern const uint8_t gm_sk9dt92p27k0o[53];
extern const uint8_t gm_sl1botv4tp1fk[58];
extern const uint8_t gm_slbqo2q5mg776[107];
extern const uint8_t gm_sn9h18hshc2p3[60];
extern const uint8_t gm_so77flgm8klar[52];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src786i8duem00q[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsk3h2du2ldnc[];
extern const gm_tmpl gm_tpl86i0s811rrlp;
extern const gm_tmpl gm_tplp3l2s17nqpoi;
extern const gm_tmpl gm_tpluc9sbr66n218;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_c_union_match_2tgsktr F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_tb06a6e((void *)&gm_l2, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcsk3h2du2ldnc, 478, 63);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l26i0t5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_l26i0t5 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tcmn71c F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_count_omtt6qd F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 48));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_to0f5qn75cgg8 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ho0p9pu F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_remap_binds_kao301q *)((&F.h)->up))->l1));
    gm_f_remap_binds_kao301q(t2, t3);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_d277g96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d277g96 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so77flgm8klar));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplp3l2s17nqpoi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t8lugsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t8lugsu F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl1botv4tp1fk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluc9sbr66n218), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tb06a6e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8fq66mhsgcib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(103));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl86i0s811rrlp), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_br4lm90 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_l26i0t5();
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rtces00();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
        gm_t9805qtonbsag t6;
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
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
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
    uint32_t t27 = gm_l2;
    gm_t9805qtonbsag t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
        memset(&(*(gm_td8hfgklo8dil *)gm_out), 0, sizeof (*(gm_td8hfgklo8dil *)gm_out));
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
        gm_t66j1im9ip128 t25 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&gm_l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out)), t25);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
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
        memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
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
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 40), 40);
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
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_td8hfgklo8dil gm_l2;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 56);
    gm_tqetrgo0q24b5 t6;
    gm_f_pop_dt2bd2s((void *)&t6, t5);
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 616);
    gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
    {
        {
            {
                void * t10 = gm_l0;
                void * t11 = (void *)(((uint8_t *)t10) + 656);
                gm_f_pop_8sodrk9((void *)&gm_l2, t11);
            }
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l2))[32] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 32);
            }
            {
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, ((uint8_t *)t13));
                gm_f_restore_scope_edqq1v8(t14, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2uqiirs(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2uqiirs F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_l26i0t5();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_i2ujhou(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_r9g5q6n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t5pgl2t(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)&F.l1), 120);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ltrd0kq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ltrd0kq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_tcmn71c();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hm23q6l(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o F;
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
        gm_b16_8 t9 = gm_f_empty_slice_rtces00();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_94j8fj1(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm23q6l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hm23q6l F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_i2ujhou(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_i2ujhou F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
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
__attribute__((aligned(4))) void * gm_f_arm_pattern_rftjhhb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_arm_pattern_rftjhhb F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* pats */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* p */
    void * gm_l10; /* pt */
    uint8_t gm_l11; /* same */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l14; /* b */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l18; /* b */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l21; /* f */
    gm_t88rcucg1b7uk gm_l22; /* t1 */
    gm_t88rcucg1b7uk gm_l23; /* t2 */
    gm_tl874t2atnlh3 gm_l24; /* span */
    GM_STACK_CHECK(&F, 1578u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    {
        gm_trqfvmjtt4sij t22;
        gm_f_list_6l26q4a((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t22;
    }
    {
        gm_trqfvmjtt4sij t23;
        gm_f_list_nh3d5t2((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t23;
    }
    gm_l6 = ((uint64_t)0ULL);
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l5 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l6;
            uint64_t t26 = gm_l5;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l6;
                        gm_l7 = t28;
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t31 = gm_l7;
                        void * t32 = (void *)(((uint8_t *)t29) + (uintptr_t)t31 * 64);
                        gm_l8 = t32;
                        gm_trqfvmjtt4sij t33;
                        gm_f_list_6l26q4a((void *)&t33, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l9 = t33;
                        void * t34 = gm_l0;
                        void * t35 = gm_l8;
                        gm_t88rcucg1b7uk t36 = gm_l2;
                        void * t37 = (void *)((uint8_t *)&F.l9);
                        gm_t9qkh0rsooc8d t38;
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
                        void * t39 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t34, t35, t36, t38, t37);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t39;
                        uint64_t t40 = gm_l7;
                        uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
                        if (t41) {
                            memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l9), 40);
                        } else {
                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                            uint8_t t44 = ((t42 == t43));
                            gm_l11 = t44;
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                            gm_l12 = t45;
                            gm_l13 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t46 = gm_l13;
                                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint8_t t48 = (t46 < t47);
                                    if (!t48) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t49 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                        uint64_t t51 = gm_l13;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l14), (((uint8_t *)t49) + (uintptr_t)t51 * 32), 32);
                                                void * t52 = (void *)((uint8_t *)&F.l3);
                                                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                                                uint8_t t54 = gm_f_bound_n051ued(t52, t53);
                                                uint8_t t55 = (uint8_t)!t54;
                                                if (t55) {
                                                    gm_l11 = ((uint8_t)0);
                                                }
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 131, 13);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t56 = gm_l13;
                                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                                    uint64_t t59 = r57;
                                    gm_l13 = t59;
                                }
                            }
gm_lb6:;
                            uint8_t t60 = gm_l11;
                            uint8_t t61 = (uint8_t)!t60;
                            if (t61) {
                                void * t62 = gm_l0;
                                void * t63 = gm_l8;
                                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 48));
                                gm_b16_8 t65;
                                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sk9dt92p27k0o));
                                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(52));
                                gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t64, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            gm_trqfvmjtt4sij t67;
                            gm_f_list_br4lm90((void *)&t67, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l15 = t67;
                            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                            gm_l16 = t68;
                            gm_l17 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t69 = gm_l17;
                                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint8_t t71 = (t69 < t70);
                                    if (!t71) {
                                        goto gm_lb11;
                                    }
                                    {
                                        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t74 = gm_l17;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l18), (((uint8_t *)t72) + (uintptr_t)t74 * 32), 32);
                                                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                                gm_l19 = t75;
                                                gm_l20 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t76 = gm_l20;
                                                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                        uint8_t t78 = (t76 < t77);
                                                        if (!t78) {
                                                            goto gm_lb16;
                                                        }
                                                        {
                                                            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                            uint64_t t81 = gm_l20;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&gm_l21), (((uint8_t *)t79) + (uintptr_t)t81 * 32), 32);
                                                                    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                                                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                                    uint8_t t84 = (gm_eq0iqiubfm7e(((uint8_t *)&t82), ((uint8_t *)&t83)));
                                                                    if (t84) {
                                                                        void * t85 = gm_l0;
                                                                        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 16));
                                                                        uint64_t t87 = GM_LD(uint64_t, ((((uint8_t *)t85) + 16) + 8));
                                                                        uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                                                                        uint64_t t89 = (uint64_t)t88;
                                                                        if ((uintptr_t)t89 >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)t89, t87, gm_src786i8duem00q, 140, 31);
                                                                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t86) + (uintptr_t)t89 * 40) + 16));
                                                                        gm_l22 = t90;
                                                                        void * t91 = gm_l0;
                                                                        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 16));
                                                                        uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 16) + 8));
                                                                        uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                                                                        uint64_t t95 = (uint64_t)t94;
                                                                        if ((uintptr_t)t95 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t95, t93, gm_src786i8duem00q, 141, 31);
                                                                        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t92) + (uintptr_t)t95 * 40) + 16));
                                                                        gm_l23 = t96;
                                                                        gm_t88rcucg1b7uk t97 = gm_l22;
                                                                        gm_t88rcucg1b7uk t98 = gm_l23;
                                                                        uint8_t t99 = (!gm_eq9m6mvtmgde(((uint8_t *)&t97), ((uint8_t *)&t98)));
                                                                        if (t99) {
                                                                            void * t100 = gm_l0;
                                                                            gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 20));
                                                                            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                                            void * t103 = gm_l0;
                                                                            gm_t88rcucg1b7uk t104 = gm_l23;
                                                                            gm_b16_8 t105 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t103, t104);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            void * t106 = gm_l0;
                                                                            gm_t88rcucg1b7uk t107 = gm_l22;
                                                                            gm_b16_8 t108 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t106, t107);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_tfeab279c16rr t109;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t102);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 16), t105);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 32), t108);
                                                                            gm_f_format_t8lugsu((void *)&gm_l25, (void *)NULL, cx, NULL, &t109);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                                            if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src786i8duem00q, 143, 46);
                                                                            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                                                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                                                            gm_b0_1 t113 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t100, t101, t112);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        void * t114 = (void *)((uint8_t *)&F.l15);
                                                                        uint32_t t115 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                                                                        uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                                                                        gm_tcc1rq4n1avb2 t117;
                                                                        GM_ST(uint32_t, ((uint8_t *)&t117 + 0), t115);
                                                                        GM_ST(uint32_t, ((uint8_t *)&t117 + 4), t116);
                                                                        gm_tftb1odc739t5 t118 = gm_f_push_2uqiirs(t114, GM_CONV(gm_cs9ufftihtbpo2, t117));
                                                                        gm_l26 = t118;
                                                                        uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                                        if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src786i8duem00q, 145, 25);
                                                                        goto gm_lb16;
                                                                    }
                                                                    goto gm_lb19;
                                                                }
gm_lb20:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 138, 17);
                                                            }
gm_lb19:;
                                                        }
gm_lb18:;
                                                        uint64_t t120 = gm_l20;
                                                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                                                        uint64_t t123 = r121;
                                                        gm_l20 = t123;
                                                    }
                                                }
gm_lb16:;
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 137, 13);
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    uint64_t t124 = gm_l17;
                                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                                    uint64_t t127 = r125;
                                    gm_l17 = t127;
                                }
                            }
gm_lb11:;
                            void * t128 = gm_l10;
                            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                            gm_f_remap_binds_kao301q(t128, t130);
                        }
                        void * t131 = (void *)((uint8_t *)&F.l4);
                        void * t132 = gm_l10;
                        gm_tftb1odc739t5 t133 = gm_f_push_u2jka7o(t131, t132);
                        gm_l27 = t133;
                        uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src786i8duem00q, 152, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 121, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t135 = gm_l6;
            uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
            uint64_t t138 = r136;
            gm_l6 = t138;
        }
    }
gm_lb1:;
    void * t139 = gm_l0;
    void * t140 = (void *)((uint8_t *)&F.l3);
    gm_b0_1 t141 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t139, t140);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t143 = ((t142 == ((uint64_t)1ULL)));
    if (t143) {
        void * t144 = GM_LD(void *, ((uint8_t *)&F.l4));
        uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t145)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t145, gm_src786i8duem00q, 155, 37);
        void * t146 = GM_LD(void *, (((uint8_t *)t144) + (uintptr_t)((uint64_t)0ULL) * 8));
        F.ret = t146;
        goto gm_fret;
    }
    void * t147 = GM_LD(void *, ((uint8_t *)&F.l4));
    uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t148)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t148, gm_src786i8duem00q, 156, 23);
    void * t149 = GM_LD(void *, (((uint8_t *)t147) + (uintptr_t)((uint64_t)0ULL) * 8));
    gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t149) + 120));
    void * t151 = GM_LD(void *, ((uint8_t *)&F.l4));
    uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint64_t r154; bool o155 = __builtin_sub_overflow(t153, ((uint64_t)1ULL), &r154);
    if (o155) gm_trap("integer overflow in `-`", gm_src786i8duem00q, 156, 54);
    uint64_t t156 = r154;
    if ((uintptr_t)t156 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t156, t152, gm_src786i8duem00q, 156, 43);
    void * t157 = GM_LD(void *, (((uint8_t *)t151) + (uintptr_t)t156 * 8));
    gm_tl874t2atnlh3 t158 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t157) + 120));
    gm_tl874t2atnlh3 t159 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t150), GM_CONV(gm_cseahvg8v7p0vs, t158)));
    gm_l24 = t159;
    {
        gm_t88rcucg1b7uk t160 = gm_l2;
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_tl874t2atnlh3 t162 = gm_l24;
        gm_tb38v5s9j1r32 t163;
        memset(&t163, 0, sizeof t163);
        GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t163 + 8), t161);
        void * t164 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t160, &t163, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t164;
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
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_binding_cond_v86td9t F;
    void * gm_l0; /* cx */
    void * gm_l1; /* pat */
    void * gm_l2; /* value */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* vty */
    void * gm_l6; /* p */
    uint8_t gm_l7; /* optional */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l8;
    void * t10 = gm_l0;
    void * t11 = gm_l2;
    gm_tkn42u7na3t81 t12;
    memset(&t12, 0, sizeof t12);
    void * t13 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t13;
    void * t14 = gm_l0;
    void * t15 = (void *)(((uint8_t *)t14) + 96);
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = gm_l3;
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t18));
    gm_t88rcucg1b7uk t20 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t15, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t20;
    {
        gm_trqfvmjtt4sij t21;
        gm_f_list_6l26q4a((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t21;
    }
    void * t22 = gm_l0;
    void * t23 = gm_l1;
    gm_t88rcucg1b7uk t24 = gm_l4;
    void * t25 = (void *)((uint8_t *)&F.l5);
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
    void * t27 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t22, t23, t24, t26, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t27;
    {
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, ((uint8_t *)t28));
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            gm_t88rcucg1b7uk t31 = gm_l4;
            uint8_t t32 = gm_f_is_error_8k27gd1(t30, GM_CONV(gm_csguankko4lmhd, t31));
            uint8_t t33 = (uint8_t)!t32;
            if (!t33) {
                goto gm_lb2;
            }
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, ((uint8_t *)t34));
            void * t36 = gm_l6;
            uint8_t t37 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t37) {
                goto gm_lb2;
            }
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, ((uint8_t *)t38));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_l4;
            gm_f_kind_jcnt4hm((void *)&gm_l8, t40, GM_CONV(gm_csguankko4lmhd, t41));
            {
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)13ULL));
                    if (!t43) {
                        goto gm_lb4;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)14ULL));
                    if (!t45) {
                        goto gm_lb5;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 184, 21);
            }
gm_lb3:;
            {
                {
                    uint8_t t46 = gm_l7;
                    if (!t46) {
                        goto gm_lb8;
                    }
                    {
                        void * t47 = gm_l1;
                        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 0));
                        uint8_t t49 = (uint8_t)((t48 == ((uint8_t)0ULL)));
                        if (!t49) {
                            goto gm_lb8;
                        }
                    }
                    void * t50 = gm_l0;
                    void * t51 = gm_l1;
                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 48));
                    gm_b16_8 t53;
                    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sa8p4rum5g6ig));
                    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(115));
                    gm_b0_1 t54 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t52, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                void * t55 = gm_l0;
                void * t56 = gm_l1;
                gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 48));
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sn9h18hshc2p3));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(59));
                gm_b0_1 t59 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t55, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb7:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t60 = gm_l0;
    void * t61 = (void *)((uint8_t *)&F.l5);
    gm_b0_1 t62 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t60, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        void * t63 = gm_l6;
        void * t64 = gm_l3;
        gm_t3mf58gv9kf3n t65;
        GM_ST(void *, ((uint8_t *)&t65 + 0), t63);
        GM_ST(void *, ((uint8_t *)&t65 + 8), t64);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 0), ((uint8_t)1ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 8), t65);
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
__attribute__((aligned(4))) uint8_t gm_f_binds_unknown_name_7m00m7a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_binds_unknown_name_7m00m7a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t08olq97rscn3 gm_l2; /* n */
    void * gm_l3; /* q */
    gm_tagc77imt2chm gm_l4; /* pp */
    gm_b16_8 gm_l5; /* args */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* args */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 752u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tqvu78rpi6q1h gm_l15;
    uint8_t gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    void * t24 = gm_l1;
    {
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)1ULL));
            if (!t26) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t24) + 8) + 8), 32);
            }
            {
                void * t27 = gm_l0;
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_f_lookup_local_6f38t7p((void *)&gm_l15, t27, t28);
            }
            {
                {
                    uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l15))[208] != 0);
                    if (t29) {
                        goto gm_lb4;
                    }
                    gm_l14 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l14 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 212, 28);
            }
gm_lb3:;
            uint8_t t30 = gm_l14;
            if (t30) {
                void * t31 = gm_l0;
                void * t32 = GM_LD(void *, ((uint8_t *)t31));
                void * t33 = gm_l0;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_tf5srh4mp4b3i t36 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t32, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t36;
                {
                    {
                        uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
                        if (t37) {
                            goto gm_lb7;
                        }
                        gm_l16 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        gm_l16 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 212, 64);
                }
gm_lb6:;
                uint8_t t38 = gm_l16;
                gm_l13 = t38;
            } else {
                gm_l13 = ((uint8_t)0);
            }
            uint8_t t39 = gm_l13;
            F.ret = t39;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)6ULL));
            if (!t41) {
                goto gm_lb9;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
            gm_l3 = t42;
            void * t43 = gm_l0;
            void * t44 = gm_l3;
            uint8_t t45 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t45;
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)7ULL));
            if (!t47) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t24) + 8) + 8), 32);
            uint8_t t48 = (uint8_t)(((((uint8_t *)&gm_l4) + 8))[16] != 0);
            if (t48) {
                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l5 = t49;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l7 = ((uint64_t)0ULL);
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            gm_l6 = t50;
            {
                for (;;) {
                    uint64_t t51 = gm_l7;
                    uint64_t t52 = gm_l6;
                    uint8_t t53 = (t51 < t52);
                    if (!t53) {
                        goto gm_lb11;
                    }
                    {
                        {
                            {
                                uint64_t t54 = gm_l7;
                                gm_l8 = t54;
                                void * t55 = gm_l0;
                                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint64_t t58 = gm_l8;
                                void * t59 = (void *)((((uint8_t *)t56) + (uintptr_t)t58 * 120) + 40);
                                uint8_t t60 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t55, t59);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t60) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 216, 13);
                        }
gm_lb14:;
                    }
gm_lb13:;
                    uint64_t t61 = gm_l7;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l7 = t64;
                }
            }
gm_lb11:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb10:;
        {
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)8ULL));
            if (!t66) {
                goto gm_lb16;
            }
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 8) + 8));
            gm_l9 = t67;
            gm_l11 = ((uint64_t)0ULL);
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            gm_l10 = t68;
            {
                for (;;) {
                    uint64_t t69 = gm_l11;
                    uint64_t t70 = gm_l10;
                    uint8_t t71 = (t69 < t70);
                    if (!t71) {
                        goto gm_lb17;
                    }
                    {
                        {
                            {
                                uint64_t t72 = gm_l11;
                                gm_l12 = t72;
                                void * t73 = gm_l0;
                                void * t74 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint64_t t76 = gm_l12;
                                void * t77 = (void *)((((uint8_t *)t74) + (uintptr_t)t76 * 120) + 40);
                                uint8_t t78 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t73, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t78) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 222, 13);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t79 = gm_l11;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l11 = t82;
                }
            }
gm_lb17:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb16:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 211, 5);
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
inline __attribute__((aligned(4))) void gm_f_check_arms_65v4isv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_check_arms_65v4isv F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* arms */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l7; /* a */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l8;
    uint8_t gm_l9;
    gm_tftb1odc739t5 gm_l10;
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_nh3d5t2((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t10;
    }
    {
        gm_trqfvmjtt4sij t11;
        gm_f_list_s83jn2q((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t11;
    }
    gm_b16_8 t12 = gm_l2;
    gm_l5 = t12;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l6;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t18 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)t16) + (uintptr_t)t18 * 24), 24);
                        void * t19 = (void *)((uint8_t *)&F.l3);
                        void * t20 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        gm_tftb1odc739t5 t21 = gm_f_push_u2jka7o(t19, t20);
                        gm_l8 = t21;
                        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src786i8duem00q, 82, 9);
                        void * t23 = (void *)((uint8_t *)&F.l4);
                        {
                            {
                                uint8_t t24 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l7) + 8)) != 0);
                                if (t24) {
                                    goto gm_lb7;
                                }
                                gm_l9 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l9 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 83, 22);
                        }
gm_lb6:;
                        uint8_t t25 = gm_l9;
                        gm_tftb1odc739t5 t26 = gm_f_push_vd1290p(t23, t25);
                        gm_l10 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src786i8duem00q, 83, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 81, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l6;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l6 = t31;
        }
    }
gm_lb1:;
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, ((uint8_t *)t32));
        gm_t88rcucg1b7uk t34 = gm_l1;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_f_check_match_mj577a4((void *)&(*(gm_tqs4ilbobv1qq *)gm_out), (void *)NULL, cx, NULL, t33, t34, t36, t38);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_deferred_match_742e2u5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    gm_tf5srh4mp4b3i gm_l4; /* found */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_tbgd14esf0jvi gm_l8; /* d */
    uint64_t gm_l9; /* i */
    gm_tbgd14esf0jvi gm_l10; /* d */
    GM_STACK_CHECK(&F, 682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdlq2ugehok8g gm_l11;
    {
        {
            void * t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)44ULL));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t12;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((((uint8_t *)t9) + 8) + 8) + 8));
            gm_l3 = t13;
            gm_tf5srh4mp4b3i t14;
            memset(&t14, 0, sizeof t14);
            gm_l4 = t14;
            void * t15 = gm_l0;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 480));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_l5 = t17;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t20 = (t18 < t19);
                    if (!t20) {
                        goto gm_lb3;
                    }
                    {
                        {
                            uint64_t t21 = gm_l6;
                            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t24 = gm_l6;
                            gm_tbgd14esf0jvi t25 = GM_LD(gm_tbgd14esf0jvi, (((uint8_t *)t22) + (uintptr_t)t24 * 32));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l11 + 0), t21);
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&gm_l11 + 8), t25);
                        }
                        {
                            {
                                uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                                gm_l7 = t26;
                                memmove(((uint8_t *)&gm_l8), (((uint8_t *)&gm_l11) + 8), 32);
                                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l8));
                                void * t28 = gm_l1;
                                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
                                uint8_t t30 = (gm_eqff85ac10o3(((uint8_t *)&t27), ((uint8_t *)&t29)));
                                if (t30) {
                                    uint64_t t31 = gm_l7;
                                    gm_tf5srh4mp4b3i t32;
                                    GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t32), t31);
                                    ((uint8_t *)&t32)[8] = 1;
                                    gm_l4 = t32;
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 105, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
gm_lb3:;
            uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
            if (t37) {
                uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
                gm_l9 = t38;
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                void * t39 = gm_l0;
                void * t40 = (void *)(((uint8_t *)t39) + 480);
                uint64_t t41 = gm_l9;
                gm_f_swap_remove_lfi9t15((void *)&gm_l10, t40, t41);
            }
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, ((uint8_t *)t42));
            void * t44 = (void *)(((uint8_t *)t43) + 8);
            void * t45 = gm_l2;
            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t45));
            uint8_t t47 = gm_f_is_error_8k27gd1(t44, GM_CONV(gm_csguankko4lmhd, t46));
            if (t47) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                void * t48 = gm_l0;
                void * t49 = gm_l0;
                void * t50 = gm_l2;
                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
                gm_b16_8 t52 = gm_l3;
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_tqs4ilbobv1qq t54;
                gm_f_check_arms_65v4isv((void *)&t54, (void *)NULL, cx, NULL, t49, t51, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l10));
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                gm_b0_1 t58 = gm_f_report_match_d77kbfe((void *)NULL, cx, NULL, t48, &t54, t55, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_wildcard_jgbf94d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_wildcard_jgbf94d F;
    void * gm_l0; /* p */
    void * gm_l1; /* q */
    gm_tagc77imt2chm gm_l2; /* pp */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* args */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t18 = gm_l0;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)6ULL));
            if (!t22) {
                goto gm_lb3;
            }
            void * t23 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 8));
            gm_l1 = t23;
            void * t24 = gm_l1;
            uint8_t t25 = gm_f_contains_wildcard_jgbf94d(t24);
            F.ret = t25;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)7ULL));
            if (!t27) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t18) + 8) + 8), 32);
            uint8_t t28 = (uint8_t)(((((uint8_t *)&gm_l2) + 8))[16] != 0);
            if (t28) {
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
                gm_l3 = t29;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l5 = ((uint64_t)0ULL);
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l4 = t30;
            {
                for (;;) {
                    uint64_t t31 = gm_l5;
                    uint64_t t32 = gm_l4;
                    uint8_t t33 = (t31 < t32);
                    if (!t33) {
                        goto gm_lb5;
                    }
                    {
                        {
                            {
                                uint64_t t34 = gm_l5;
                                gm_l6 = t34;
                                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t37 = gm_l6;
                                void * t38 = (void *)((((uint8_t *)t35) + (uintptr_t)t37 * 120) + 40);
                                uint8_t t39 = gm_f_contains_wildcard_jgbf94d(t38);
                                if (t39) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 366, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_lb5:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)8ULL));
            if (!t45) {
                goto gm_lb10;
            }
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 8) + 8));
            gm_l7 = t46;
            gm_l9 = ((uint64_t)0ULL);
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            gm_l8 = t47;
            {
                for (;;) {
                    uint64_t t48 = gm_l9;
                    uint64_t t49 = gm_l8;
                    uint8_t t50 = (t48 < t49);
                    if (!t50) {
                        goto gm_lb11;
                    }
                    {
                        {
                            {
                                uint64_t t51 = gm_l9;
                                gm_l10 = t51;
                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t54 = gm_l10;
                                void * t55 = (void *)((((uint8_t *)t52) + (uintptr_t)t54 * 120) + 40);
                                uint8_t t56 = gm_f_contains_wildcard_jgbf94d(t55);
                                if (t56) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 372, 13);
                        }
gm_lb14:;
                    }
gm_lb13:;
                    uint64_t t57 = gm_l9;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l9 = t60;
                }
            }
gm_lb11:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb10:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 361, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_expr_to_pattern_rchr7ig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_expr_to_pattern_rchr7ig F;
    void * gm_l0; /* e */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* a */
    gm_t08olq97rscn3 gm_l11; /* n */
    gm_t9qkh0rsooc8d gm_l14; /* op */
    void * gm_l15; /* operand */
    gm_b16_8 gm_l17; /* elems */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* el */
    void * gm_l24; /* inner */
    GM_STACK_CHECK(&F, 2184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tq7s98ihonkju gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_toqf51949ppls gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_toqf51949ppls gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_toqf51949ppls gm_l32;
    gm_tq7s98ihonkju gm_l33;
    uint8_t gm_l34;
    gm_tq7s98ihonkju gm_l35;
    gm_tq7s98ihonkju gm_l36;
    gm_toqf51949ppls gm_l37;
    gm_tftb1odc739t5 gm_l38;
    {
        memset(&F.l1, 0, sizeof F.l1);
        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)0ULL));
        void * t46 = gm_l0;
        {
            {
                {
                    uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)7ULL));
                    if (!t48) {
                        goto gm_lb2;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)4ULL));
                    if (!t50) {
                        goto gm_lb3;
                    }
                    memset(&F.l1, 0, sizeof F.l1);
                    GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)4ULL));
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    {
                        {
                            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)8ULL));
                            if (!t52) {
                                goto gm_lb6;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)22ULL));
                            if (!t54) {
                                goto gm_lb7;
                            }
                            goto gm_lb5;
                        }
gm_lb7:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        void * t55 = gm_l0;
                        gm_tq7s98ihonkju t56 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t55)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ku35m9tsdej)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l25 = t56;
                    }
                    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src786i8duem00q, 307, 73);
                    {
                        void * t58 = GM_LD(void *, (((uint8_t *)&gm_l25) + 8));
                        gm_th2lqbo77pvsc t59;
                        memset(&t59, 0, sizeof t59);
                        gm_tagc77imt2chm t60;
                        GM_ST(void *, ((uint8_t *)&t60 + 0), t58);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t60 + 8), t59);
                        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_tagc77imt2chm, ((uint8_t *)&F.l1 + 8), t60);
                    }
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)19ULL));
                    if (!t62) {
                        goto gm_lb8;
                    }
                    void * t63 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
                    gm_l2 = t63;
                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((((uint8_t *)t46) + 8) + 8) + 8));
                    gm_l3 = t64;
                    {
                        {
                            void * t65 = gm_l2;
                            {
                                {
                                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
                                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)8ULL));
                                    if (!t67) {
                                        goto gm_lb12;
                                    }
                                    gm_l26 = ((uint8_t)0);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    gm_l26 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 309, 16);
                            }
gm_lb11:;
                            uint8_t t68 = gm_l26;
                            if (!t68) {
                                goto gm_lb10;
                            }
                            void * t69 = gm_l2;
                            {
                                {
                                    uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
                                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)22ULL));
                                    if (!t71) {
                                        goto gm_lb15;
                                    }
                                    gm_l27 = ((uint8_t)0);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                {
                                    gm_l27 = ((uint8_t)1);
                                    goto gm_lb14;
                                }
gm_lb16:;
                                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 309, 52);
                            }
gm_lb14:;
                            uint8_t t72 = gm_l27;
                            if (!t72) {
                                goto gm_lb10;
                            }
                            memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                            goto gm_fdone;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        gm_trqfvmjtt4sij t73;
                        gm_f_list_ttksl9g((void *)&t73, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l4 = t73;
                    }
                    gm_l6 = ((uint64_t)0ULL);
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    gm_l5 = t74;
                    {
                        for (;;) {
                            uint64_t t75 = gm_l6;
                            uint64_t t76 = gm_l5;
                            uint8_t t77 = (t75 < t76);
                            if (!t77) {
                                goto gm_lb17;
                            }
                            {
                                {
                                    {
                                        uint64_t t78 = gm_l6;
                                        gm_l7 = t78;
                                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t81 = gm_l7;
                                        void * t82 = (void *)(((uint8_t *)t79) + (uintptr_t)t81 * 144);
                                        gm_l8 = t82;
                                        void * t83 = gm_l8;
                                        {
                                            {
                                                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)t83) + 0));
                                                uint8_t t85 = (uint8_t)(t84 == ((uint8_t)0ULL));
                                                if (!t85) {
                                                    goto gm_lb23;
                                                }
                                                {
                                                    memmove(((uint8_t *)&F.l9), (((uint8_t *)t83) + 8), 88);
                                                }
                                                {
                                                    void * t86 = (void *)((uint8_t *)&F.l9);
                                                    gm_f_nested_pattern_oiclpgf((void *)&gm_l28, (void *)NULL, cx, NULL, t86);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l28))[64] != 0);
                                                if (t87) {
                                                    memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l28), 64);
                                                } else {
                                                    memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                                                    goto gm_fdone;
                                                }
                                                {
                                                    void * t88 = (void *)((uint8_t *)&F.l4);
                                                    gm_t1s75i08qtptd t89;
                                                    memset(&t89, 0, sizeof t89);
                                                    void * t90 = gm_l8;
                                                    gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 128));
                                                    gm_t0d02142k4tu8 t92 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l10));
                                                    gm_tnrgict8sthcv t93;
                                                    GM_ST(uint32_t, (uint8_t *)&t93 + 116, 0);
                                                    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t93 + 0), t89);
                                                    GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t93 + 40), t92);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t93 + 104), t91);
                                                    gm_tftb1odc739t5 t94 = gm_f_push_l74t9rq(t88, &t93);
                                                    gm_l29 = t94;
                                                }
                                                uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                                if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src786i8duem00q, 316, 25);
                                                goto gm_lb22;
                                            }
gm_lb23:;
                                            {
                                                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t83) + 0));
                                                uint8_t t97 = (uint8_t)(t96 == ((uint8_t)1ULL));
                                                if (!t97) {
                                                    goto gm_lb24;
                                                }
                                                {
                                                    memmove(((uint8_t *)&gm_l11), (((uint8_t *)t83) + 8), 32);
                                                }
                                                {
                                                    memmove(((uint8_t *)&F.l12), ((((uint8_t *)t83) + 8) + 32), 88);
                                                }
                                                {
                                                    void * t98 = (void *)((uint8_t *)&F.l12);
                                                    gm_f_nested_pattern_oiclpgf((void *)&gm_l30, (void *)NULL, cx, NULL, t98);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t99 = (uint8_t)((((uint8_t *)&gm_l30))[64] != 0);
                                                if (t99) {
                                                    memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l30), 64);
                                                } else {
                                                    memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                                                    goto gm_fdone;
                                                }
                                                {
                                                    void * t100 = (void *)((uint8_t *)&F.l4);
                                                    gm_t08olq97rscn3 t101 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l11));
                                                    gm_t1s75i08qtptd t102;
                                                    GM_ST(uint8_t, (uint8_t *)&t102 + 33, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t102 + 34, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t102 + 36, 0);
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t102), t101);
                                                    ((uint8_t *)&t102)[32] = 1;
                                                    void * t103 = gm_l8;
                                                    gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t103) + 128));
                                                    gm_t0d02142k4tu8 t105 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l13));
                                                    gm_tnrgict8sthcv t106;
                                                    GM_ST(uint32_t, (uint8_t *)&t106 + 116, 0);
                                                    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t106 + 0), t102);
                                                    GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t106 + 40), t105);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t106 + 104), t104);
                                                    gm_tftb1odc739t5 t107 = gm_f_push_l74t9rq(t100, &t106);
                                                    gm_l31 = t107;
                                                }
                                                uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src786i8duem00q, 320, 25);
                                                goto gm_lb22;
                                            }
gm_lb24:;
                                            {
                                                uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)t83) + 0));
                                                uint8_t t110 = (uint8_t)(t109 == ((uint8_t)2ULL));
                                                if (!t110) {
                                                    goto gm_lb25;
                                                }
                                                memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                                                goto gm_fdone;
                                            }
gm_lb25:;
                                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 313, 17);
                                        }
gm_lb22:;
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 311, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t111 = gm_l6;
                            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                            uint64_t t114 = r112;
                            gm_l6 = t114;
                        }
                    }
gm_lb17:;
                    {
                        void * t115 = gm_l2;
                        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_th2lqbo77pvsc t117;
                        GM_ST(uint8_t, (uint8_t *)&t117 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t117 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t117 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t117), t116);
                        ((uint8_t *)&t117)[16] = 1;
                        gm_tagc77imt2chm t118;
                        GM_ST(void *, ((uint8_t *)&t118 + 0), t115);
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t118 + 8), t117);
                        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_tagc77imt2chm, ((uint8_t *)&F.l1 + 8), t118);
                    }
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t120 = (uint8_t)(t119 == ((uint8_t)12ULL));
                    if (!t120) {
                        goto gm_lb26;
                    }
                    gm_t9qkh0rsooc8d t121 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t46) + 8) + 8));
                    gm_l14 = t121;
                    void * t122 = GM_LD(void *, (((((uint8_t *)t46) + 8) + 8) + 8));
                    gm_l15 = t122;
                    gm_t9qkh0rsooc8d t123 = gm_l14;
                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&t123) + 0));
                    uint8_t t125 = (uint8_t)((t124 == ((uint8_t)3ULL)));
                    gm_t9qkh0rsooc8d t126;
                    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)3ULL));
                    if (t125) {
                        {
                            void * t127 = gm_l15;
                            gm_f_expr_to_pattern_rchr7ig((void *)&gm_l32, (void *)NULL, cx, NULL, t127);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t128 = (uint8_t)((((uint8_t *)&gm_l32))[64] != 0);
                        if (t128) {
                            memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l32), 64);
                        } else {
                            memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                            goto gm_fdone;
                        }
                        {
                            gm_tq7s98ihonkju t129 = gm_f_new_f7qroea((void *)NULL, cx, NULL, &(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l16)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdhojj6r45o)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l33 = t129;
                        }
                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src786i8duem00q, 330, 29);
                        {
                            void * t131 = GM_LD(void *, (((uint8_t *)&gm_l33) + 8));
                            GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l1 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l1 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)6ULL));
                            GM_ST(void *, ((uint8_t *)&F.l1 + 8), t131);
                        }
                    } else {
                        {
                            {
                                gm_t9qkh0rsooc8d t132 = gm_l14;
                                uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t132) + 0));
                                uint8_t t134 = (uint8_t)((t133 == ((uint8_t)0ULL)));
                                if (!t134) {
                                    goto gm_lb28;
                                }
                                void * t135 = gm_l15;
                                gm_t9qkh0rsooc8d t136;
                                GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)0ULL));
                                {
                                    {
                                        uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t135) + 8) + 0));
                                        uint8_t t138 = (uint8_t)(t137 == ((uint8_t)0ULL));
                                        if (!t138) {
                                            goto gm_lb30;
                                        }
                                        gm_l34 = ((uint8_t)1);
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                    {
                                        gm_l34 = ((uint8_t)0);
                                        goto gm_lb29;
                                    }
gm_lb31:;
                                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 331, 38);
                                }
gm_lb29:;
                                uint8_t t139 = gm_l34;
                                if (!t139) {
                                    goto gm_lb28;
                                }
                                {
                                    void * t140 = gm_l0;
                                    gm_tq7s98ihonkju t141 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t140)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9foool24ve)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l35 = t141;
                                }
                                uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src786i8duem00q, 332, 29);
                                {
                                    void * t143 = GM_LD(void *, (((uint8_t *)&gm_l35) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l1 + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&F.l1 + 32, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)3ULL));
                                    GM_ST(void *, ((uint8_t *)&F.l1 + 8), t143);
                                }
                                goto gm_lb27;
                            }
gm_lb28:;
                            memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                            goto gm_fdone;
                        }
gm_lb27:;
                    }
                    goto gm_lb1;
                }
gm_lb26:;
            }
            {
                {
                    {
                        {
                            uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t145 = (uint8_t)(t144 == ((uint8_t)0ULL));
                            if (!t145) {
                                goto gm_lb34;
                            }
                            goto gm_lb33;
                        }
gm_lb34:;
                        {
                            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t147 = (uint8_t)(t146 == ((uint8_t)2ULL));
                            if (!t147) {
                                goto gm_lb35;
                            }
                            goto gm_lb33;
                        }
gm_lb35:;
                        {
                            uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                            uint8_t t149 = (uint8_t)(t148 == ((uint8_t)3ULL));
                            if (!t149) {
                                goto gm_lb36;
                            }
                            goto gm_lb33;
                        }
gm_lb36:;
                        goto gm_lb32;
                    }
gm_lb33:;
                    {
                        void * t150 = gm_l0;
                        gm_tq7s98ihonkju t151 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t150)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kns860jaqph)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l36 = t151;
                    }
                    uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src786i8duem00q, 337, 53);
                    {
                        void * t153 = GM_LD(void *, (((uint8_t *)&gm_l36) + 8));
                        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l1 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l1 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)3ULL));
                        GM_ST(void *, ((uint8_t *)&F.l1 + 8), t153);
                    }
                    goto gm_lb1;
                }
gm_lb32:;
                {
                    uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t155 = (uint8_t)(t154 == ((uint8_t)10ULL));
                    if (!t155) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t156 = GM_LD(gm_b16_8, ((((uint8_t *)t46) + 8) + 8));
                    gm_l17 = t156;
                    {
                        gm_trqfvmjtt4sij t157;
                        gm_f_list_ttksl9g((void *)&t157, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l18 = t157;
                    }
                    gm_l20 = ((uint64_t)0ULL);
                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    gm_l19 = t158;
                    {
                        for (;;) {
                            uint64_t t159 = gm_l20;
                            uint64_t t160 = gm_l19;
                            uint8_t t161 = (t159 < t160);
                            if (!t161) {
                                goto gm_lb38;
                            }
                            {
                                {
                                    {
                                        uint64_t t162 = gm_l20;
                                        gm_l21 = t162;
                                        void * t163 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                        uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint64_t t165 = gm_l21;
                                        void * t166 = (void *)(((uint8_t *)t163) + (uintptr_t)t165 * 136);
                                        gm_l22 = t166;
                                        void * t167 = gm_l22;
                                        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)t167) + 128));
                                        if (t168) {
                                            memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                                            goto gm_fdone;
                                        }
                                        {
                                            void * t169 = gm_l22;
                                            void * t170 = (void *)(((uint8_t *)t169) + 40);
                                            gm_f_nested_pattern_oiclpgf((void *)&gm_l37, (void *)NULL, cx, NULL, t170);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        uint8_t t171 = (uint8_t)((((uint8_t *)&gm_l37))[64] != 0);
                                        if (t171) {
                                            memmove(((uint8_t *)&F.l23), ((uint8_t *)&gm_l37), 64);
                                        } else {
                                            memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                                            goto gm_fdone;
                                        }
                                        {
                                            void * t172 = (void *)((uint8_t *)&F.l18);
                                            void * t173 = gm_l22;
                                            void * t174 = gm_l22;
                                            gm_tl874t2atnlh3 t175 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t174) + 40) + 72));
                                            gm_t1s75i08qtptd t176 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)t173));
                                            gm_t0d02142k4tu8 t177 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l23));
                                            gm_tnrgict8sthcv t178;
                                            GM_ST(uint32_t, (uint8_t *)&t178 + 116, 0);
                                            GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t178 + 0), t176);
                                            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t178 + 40), t177);
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t178 + 104), t175);
                                            gm_tftb1odc739t5 t179 = gm_f_push_l74t9rq(t172, &t178);
                                            gm_l38 = t179;
                                        }
                                        uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                        if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src786i8duem00q, 344, 17);
                                        goto gm_lb41;
                                    }
gm_lb42:;
                                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 340, 13);
                                }
gm_lb41:;
                            }
gm_lb40:;
                            uint64_t t181 = gm_l20;
                            uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                            uint64_t t184 = r182;
                            gm_l20 = t184;
                        }
                    }
gm_lb38:;
                    {
                        gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l1 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l1 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l1 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l1 + 8), t185);
                    }
                    goto gm_lb1;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)9ULL));
                    if (!t187) {
                        goto gm_lb43;
                    }
                    void * t188 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
                    gm_l24 = t188;
                    void * t189 = gm_l24;
                    gm_f_expr_to_pattern_rchr7ig((void *)&(*(gm_toqf51949ppls *)gm_out), (void *)NULL, cx, NULL, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb43:;
                {
                    memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                    goto gm_fdone;
                }
gm_lb44:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 304, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t190 = gm_l0;
        uint32_t t191 = GM_LD(uint32_t, ((uint8_t *)t190));
        void * t192 = gm_l0;
        gm_tl874t2atnlh3 t193 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t192) + 72));
        gm_to361lgknsu69 t194 = GM_LD(gm_to361lgknsu69, ((uint8_t *)&F.l1));
        gm_t0d02142k4tu8 t195;
        GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t195 + 60, 0);
        GM_ST(uint32_t, ((uint8_t *)&t195 + 0), t191);
        GM_ST(gm_to361lgknsu69, ((uint8_t *)&t195 + 8), t194);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t195 + 48), t193);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out)), t195);
        ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_in_patterns_mi6p7hb F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lhs */
    gm_b16_8 gm_l2; /* members */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    void * gm_l11; /* m */
    void * gm_l15; /* pat */
    void * gm_l16; /* otherwise */
    GM_STACK_CHECK(&F, 1153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    void * t12 = (void *)(((uint8_t *)t11) + 8);
    gm_l5 = t12;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 96);
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    gm_t88rcucg1b7uk t19 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t14, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t19;
    void * t20 = gm_l0;
    gm_t88rcucg1b7uk t21 = gm_l6;
    uint8_t t22 = gm_f_is_c_union_p1r4eve(t20, GM_CONV(gm_csguankko4lmhd, t21));
    if (t22) {
        void * t23 = gm_l0;
        void * t24 = gm_l1;
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 72));
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, ((uint8_t *)t26));
        gm_t88rcucg1b7uk t28 = gm_l6;
        gm_b16_8 t29 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t23, t25, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t31 = gm_l5;
        gm_t88rcucg1b7uk t32 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t32;
    }
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_58nr42v_2((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t33;
    }
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l8 = t34;
    {
        for (;;) {
            uint64_t t35 = gm_l9;
            uint64_t t36 = gm_l8;
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t38 = gm_l9;
                        gm_l10 = t38;
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t41 = gm_l10;
                        void * t42 = (void *)(((uint8_t *)t39) + (uintptr_t)t41 * 136);
                        gm_l11 = t42;
                        void * t43 = gm_l11;
                        {
                            {
                                uint8_t t44 = (uint8_t)((((uint8_t *)t43))[32] != 0);
                                if (t44) {
                                    goto gm_lb7;
                                }
                                gm_l17 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l17 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 268, 30);
                        }
gm_lb6:;
                        uint8_t t45 = gm_l17;
                        if (t45) {
                            void * t46 = gm_l11;
                            void * t47 = (void *)(((uint8_t *)t46) + 40);
                            gm_f_expr_to_pattern_rchr7ig((void *)&F.l12, (void *)NULL, cx, NULL, t47);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            memset(&F.l12, 0, sizeof F.l12);
                        }
                        uint8_t t48 = (uint8_t)((((uint8_t *)&F.l12))[64] != 0);
                        if (t48) {
                            memmove(((uint8_t *)&F.l13), ((uint8_t *)&F.l12), 64);
                        } else {
                            void * t49 = gm_l0;
                            void * t50 = gm_l11;
                            gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t50) + 40) + 72));
                            gm_b16_8 t52;
                            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_slbqo2q5mg776));
                            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(106));
                            gm_b0_1 t53 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t49, t51, t52);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t54 = gm_l0;
                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                            gm_tl874t2atnlh3 t56 = gm_l4;
                            void * t57 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t57;
                            goto gm_fret;
                        }
                        {
                            gm_trqfvmjtt4sij t58;
                            gm_f_list_6l26q4a((void *)&t58, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l14 = t58;
                        }
                        void * t59 = gm_l0;
                        void * t60 = (void *)((uint8_t *)&F.l13);
                        gm_t88rcucg1b7uk t61 = gm_l6;
                        void * t62 = (void *)((uint8_t *)&F.l14);
                        gm_t9qkh0rsooc8d t63;
                        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)2ULL));
                        void * t64 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t59, t60, t61, t63, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t64;
                        {
                            void * t65 = (void *)((uint8_t *)&F.l7);
                            void * t66 = gm_l15;
                            void * t67 = gm_l5;
                            gm_t88rcucg1b7uk t68 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t67);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t69 = gm_l3;
                            uint8_t t70 = (uint8_t)!t69;
                            gm_tl874t2atnlh3 t71 = gm_l4;
                            gm_to0f5qn75cgg8 t72;
                            memset(&t72, 0, sizeof t72);
                            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)4ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t72 + 8), t70);
                            void * t73 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t68, &t72, t71);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbbqefuhv2r8h t74;
                            GM_ST(void *, ((uint8_t *)&t74 + 0), t66);
                            GM_ST(void *, ((uint8_t *)&t74 + 8), ((void *)0));
                            GM_ST(void *, ((uint8_t *)&t74 + 16), t73);
                            gm_tftb1odc739t5 t75 = gm_f_push_bpovr2k_2(t65, &t74);
                            gm_l18 = t75;
                        }
                        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src786i8duem00q, 275, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 266, 5);
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
    {
        void * t81 = gm_l5;
        gm_t88rcucg1b7uk t82 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = gm_l3;
        gm_tl874t2atnlh3 t84 = gm_l4;
        gm_to0f5qn75cgg8 t85;
        memset(&t85, 0, sizeof t85);
        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t85 + 8), t83);
        void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t85, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t86;
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l7);
        gm_t88rcucg1b7uk t88 = gm_l6;
        gm_tl874t2atnlh3 t89 = gm_l4;
        gm_tb38v5s9j1r32 t90;
        memset(&t90, 0, sizeof t90);
        GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)0ULL));
        void * t91 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t88, &t90, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t92 = gm_l16;
        gm_tbbqefuhv2r8h t93;
        GM_ST(void *, ((uint8_t *)&t93 + 0), t91);
        GM_ST(void *, ((uint8_t *)&t93 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t93 + 16), t92);
        gm_tftb1odc739t5 t94 = gm_f_push_bpovr2k_2(t87, &t93);
        gm_l19 = t94;
    }
    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src786i8duem00q, 278, 5);
    {
        void * t96 = gm_l5;
        gm_t88rcucg1b7uk t97 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t98 = gm_l1;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_tkaapcii86udk t100;
        GM_ST(void *, ((uint8_t *)&t100 + 0), t98);
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 8), t99);
        gm_tl874t2atnlh3 t101 = gm_l4;
        gm_to0f5qn75cgg8 t102;
        memset(&t102, 0, sizeof t102);
        GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t102 + 8), t100);
        void * t103 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t97, &t102, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t103;
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
inline __attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_is_pattern_operand_p51tban F;
    void * gm_l0; /* cx */
    void * gm_l1; /* rhs */
    gm_t88rcucg1b7uk gm_l2; /* lhs_ty */
    gm_tagc77imt2chm gm_l4; /* pp */
    void * gm_l5; /* base */
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_toqf51949ppls gm_l6;
    uint8_t gm_l7;
    gm_tkn42u7na3t81 gm_l8;
    uint8_t gm_l9;
    gm_t53at80pfdus3 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    {
        void * t12 = gm_l1;
        gm_f_expr_to_pattern_rchr7ig((void *)&gm_l6, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[64] != 0);
    if (t13) {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l6), 64);
    } else {
        memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
        goto gm_fdone;
    }
    {
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)7ULL));
            if (!t15) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l4), ((((uint8_t *)&F.l3) + 8) + 8), 32);
            }
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)22ULL));
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l5 = t19;
            void * t20 = gm_l0;
            void * t21 = gm_l5;
            gm_tkn42u7na3t81 t22 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t22;
            {
                {
                    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
                    if (t23) {
                        goto gm_lb4;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 201, 70);
            }
gm_lb3:;
            uint8_t t24 = gm_l7;
            if (!t24) {
                goto gm_lb2;
            }
            {
                memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, ((uint8_t *)t25));
                void * t27 = (void *)(((uint8_t *)t26) + 8);
                void * t28 = gm_l0;
                void * t29 = (void *)(((uint8_t *)t28) + 96);
                void * t30 = gm_l0;
                void * t31 = GM_LD(void *, ((uint8_t *)t30));
                gm_t88rcucg1b7uk t32 = gm_l2;
                gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_kind_jcnt4hm((void *)&gm_l10, t27, GM_CONV(gm_csguankko4lmhd, t33));
            }
            {
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)13ULL));
                    if (!t35) {
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
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 203, 8);
            }
gm_lb8:;
            uint8_t t36 = gm_l9;
            if (!t36) {
                goto gm_lb7;
            }
            {
                uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
                uint8_t t38 = (uint8_t)((t37 == ((uint8_t)4ULL)));
                if (!t38) {
                    goto gm_lb7;
                }
            }
            {
                gm_t0d02142k4tu8 t39 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l3));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
                GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out)), t39);
                ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
                goto gm_fdone;
            }
        }
gm_lb7:;
    }
gm_lb6:;
    void * t40 = (void *)((uint8_t *)&F.l3);
    uint8_t t41 = gm_f_contains_wildcard_jgbf94d(t40);
    if (t41) {
        gm_l12 = ((uint8_t)1);
    } else {
        void * t42 = gm_l0;
        void * t43 = (void *)((uint8_t *)&F.l3);
        gm_t88rcucg1b7uk t44 = gm_l2;
        uint8_t t45 = gm_f_omits_payload_e33pdef((void *)NULL, cx, NULL, t42, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t45;
    }
    uint8_t t46 = gm_l12;
    if (t46) {
        gm_l11 = ((uint8_t)1);
    } else {
        void * t47 = gm_l0;
        void * t48 = (void *)((uint8_t *)&F.l3);
        uint8_t t49 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t49;
    }
    uint8_t t50 = gm_l11;
    if (t50) {
        gm_t0d02142k4tu8 t51 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out)), t51);
        ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
        goto gm_fdone;
    }
    {
        memset(&(*(gm_toqf51949ppls *)gm_out), 0, sizeof (*(gm_toqf51949ppls *)gm_out));
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
__attribute__((aligned(4))) void * gm_f_is_pattern_test_kdd7cfh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_is_pattern_test_kdd7cfh F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lhs */
    void * gm_l2; /* p */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    void * gm_l8; /* pat */
    void * gm_l9; /* then */
    void * gm_l10; /* otherwise */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_l5 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 96);
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
    gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t11;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l6;
    uint8_t t14 = gm_f_is_c_union_p1r4eve(t12, GM_CONV(gm_csguankko4lmhd, t13));
    if (t14) {
        void * t15 = gm_l0;
        void * t16 = gm_l1;
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, ((uint8_t *)t18));
        gm_t88rcucg1b7uk t20 = gm_l6;
        gm_b16_8 t21 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t22 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t15, t17, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t23 = gm_l5;
        gm_t88rcucg1b7uk t24 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t24;
    }
    {
        gm_trqfvmjtt4sij t25;
        gm_f_list_6l26q4a((void *)&t25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t25;
    }
    void * t26 = gm_l0;
    void * t27 = gm_l2;
    gm_t88rcucg1b7uk t28 = gm_l6;
    void * t29 = (void *)((uint8_t *)&F.l7);
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
    void * t31 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t26, t27, t28, t30, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t31;
    {
        void * t32 = gm_l5;
        gm_t88rcucg1b7uk t33 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = gm_l3;
        uint8_t t35 = (uint8_t)!t34;
        gm_tl874t2atnlh3 t36 = gm_l4;
        gm_to0f5qn75cgg8 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t37 + 8), t35);
        void * t38 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t33, &t37, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t38;
    }
    {
        void * t39 = gm_l5;
        gm_t88rcucg1b7uk t40 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = gm_l3;
        gm_tl874t2atnlh3 t42 = gm_l4;
        gm_to0f5qn75cgg8 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t43 + 8), t41);
        void * t44 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t43, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t44;
    }
    {
        gm_trqfvmjtt4sij t45;
        gm_f_list_58nr42v_2((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t45;
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l11);
        void * t47 = gm_l8;
        void * t48 = gm_l9;
        gm_tbbqefuhv2r8h t49;
        GM_ST(void *, ((uint8_t *)&t49 + 0), t47);
        GM_ST(void *, ((uint8_t *)&t49 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t49 + 16), t48);
        gm_tftb1odc739t5 t50 = gm_f_push_bpovr2k_2(t46, &t49);
        gm_l12 = t50;
    }
    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src786i8duem00q, 295, 5);
    {
        void * t52 = (void *)((uint8_t *)&F.l11);
        gm_t88rcucg1b7uk t53 = gm_l6;
        gm_tl874t2atnlh3 t54 = gm_l4;
        gm_tb38v5s9j1r32 t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)0ULL));
        void * t56 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t53, &t55, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t57 = gm_l10;
        gm_tbbqefuhv2r8h t58;
        GM_ST(void *, ((uint8_t *)&t58 + 0), t56);
        GM_ST(void *, ((uint8_t *)&t58 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t58 + 16), t57);
        gm_tftb1odc739t5 t59 = gm_f_push_bpovr2k_2(t52, &t58);
        gm_l13 = t59;
    }
    uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src786i8duem00q, 296, 5);
    {
        void * t61 = gm_l5;
        gm_t88rcucg1b7uk t62 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t63 = gm_l1;
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_tkaapcii86udk t65;
        GM_ST(void *, ((uint8_t *)&t65 + 0), t63);
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t64);
        gm_tl874t2atnlh3 t66 = gm_l4;
        gm_to0f5qn75cgg8 t67;
        memset(&t67, 0, sizeof t67);
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t67 + 8), t65);
        void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t62, &t67, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t68;
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
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_match_expr_npcoo60 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* m */
    uint8_t gm_l2; /* value */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    void * gm_l4; /* tys */
    void * gm_l5; /* scrutinee */
    gm_t88rcucg1b7uk gm_l6; /* sty */
    uint8_t gm_l7; /* deferred */
    uint8_t gm_l9; /* bad_pattern */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* arm */
    uint64_t gm_l14; /* before */
    void * gm_l15; /* pat */
    void * gm_l16; /* guard */
    void * gm_l17; /* g */
    void * gm_l19; /* body */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    gm_t88rcucg1b7uk gm_l24; /* ty */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l29; /* a */
    uint8_t gm_l30; /* all_never */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l33; /* a */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    void * gm_l37; /* b */
    GM_STACK_CHECK(&F, 2424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, ((uint8_t *)t37));
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        gm_l4 = t39;
        void * t40 = gm_l0;
        void * t41 = gm_l1;
        void * t42 = GM_LD(void *, ((uint8_t *)t41));
        gm_tkn42u7na3t81 t43;
        memset(&t43, 0, sizeof t43);
        void * t44 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t40, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t44;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 96);
        void * t47 = gm_l0;
        void * t48 = GM_LD(void *, ((uint8_t *)t47));
        void * t49 = gm_l5;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t49));
        gm_t88rcucg1b7uk t51 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t46, t48, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t51;
        void * t52 = gm_l4;
        gm_t88rcucg1b7uk t53 = gm_l6;
        uint8_t t54 = gm_f_has_infer_3o82a0b(t52, GM_CONV(gm_csguankko4lmhd, t53));
        gm_l7 = t54;
        void * t55 = gm_l0;
        gm_t88rcucg1b7uk t56 = gm_l6;
        uint8_t t57 = gm_f_is_c_union_p1r4eve(t55, GM_CONV(gm_csguankko4lmhd, t56));
        if (t57) {
            void * t58 = gm_l0;
            void * t59 = gm_l1;
            void * t60 = GM_LD(void *, ((uint8_t *)t59));
            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, ((uint8_t *)t62));
            gm_t88rcucg1b7uk t64 = gm_l6;
            gm_b16_8 t65 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t58, t61, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t67 = gm_l4;
            gm_t88rcucg1b7uk t68 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t68;
        }
        gm_trqfvmjtt4sij t69;
        gm_f_list_58nr42v_2((void *)&t69, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t69;
        gm_l9 = ((uint8_t)0);
        gm_l11 = ((uint64_t)0ULL);
        void * t70 = gm_l1;
        uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)t70) + 8) + 8));
        gm_l10 = t71;
        {
            for (;;) {
                uint64_t t72 = gm_l11;
                uint64_t t73 = gm_l10;
                uint8_t t74 = (t72 < t73);
                if (!t74) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t75 = gm_l11;
                            gm_l12 = t75;
                            void * t76 = gm_l1;
                            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 8));
                            uint64_t t78 = GM_LD(uint64_t, ((((uint8_t *)t76) + 8) + 8));
                            uint64_t t79 = gm_l12;
                            if ((uintptr_t)t79 >= (uintptr_t)(t78)) gm_trap_index((uintptr_t)t79, t78, gm_src786i8duem00q, 28, 17);
                            void * t80 = (void *)(((uint8_t *)t77) + (uintptr_t)t79 * 48);
                            gm_l13 = t80;
                            void * t81 = gm_l0;
                            gm_b0_1 t82 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t81);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t83 = gm_l0;
                            void * t84 = GM_LD(void *, ((uint8_t *)t83));
                            void * t85 = GM_LD(void *, ((uint8_t *)t84));
                            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 8));
                            uint64_t t87 = gm_f_error_count_omtt6qd(t86);
                            gm_l14 = t87;
                            void * t88 = gm_l0;
                            void * t89 = gm_l13;
                            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)t89));
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                            gm_t88rcucg1b7uk t92 = gm_l6;
                            void * t93 = gm_f_arm_pattern_rftjhhb((void *)NULL, cx, NULL, t88, t91, t92);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t93;
                            void * t94 = gm_l0;
                            void * t95 = GM_LD(void *, ((uint8_t *)t94));
                            void * t96 = GM_LD(void *, ((uint8_t *)t95));
                            void * t97 = GM_LD(void *, (((uint8_t *)t96) + 8));
                            uint64_t t98 = gm_f_error_count_omtt6qd(t97);
                            uint64_t t99 = gm_l14;
                            uint8_t t100 = (t98 > t99);
                            if (t100) {
                                gm_l9 = ((uint8_t)1);
                            }
                            gm_l16 = ((void *)0);
                            {
                                {
                                    void * t101 = gm_l13;
                                    uint8_t t102 = (uint8_t)(GM_LD(void *, (((uint8_t *)t101) + 16)) != 0);
                                    if (!t102) {
                                        goto gm_lb7;
                                    }
                                    void * t103 = GM_LD(void *, (((uint8_t *)t101) + 16));
                                    gm_l17 = t103;
                                    void * t104 = gm_l0;
                                    void * t105 = gm_l0;
                                    void * t106 = gm_l17;
                                    void * t107 = gm_l4;
                                    gm_t88rcucg1b7uk t108 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t107);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tkn42u7na3t81 t109;
                                    GM_ST(uint8_t, (uint8_t *)&t109 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t109 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t109), t108);
                                    ((uint8_t *)&t109)[4] = 1;
                                    void * t110 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t105, t106, t109);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t111 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t104, t110);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l16 = t111;
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            {
                                {
                                    void * t112 = gm_l13;
                                    void * t113 = GM_LD(void *, (((uint8_t *)t112) + 24));
                                    uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
                                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)44ULL));
                                    if (!t115) {
                                        goto gm_lb9;
                                    }
                                    memmove(((uint8_t *)&F.l18), ((((uint8_t *)t113) + 8) + 8), 40);
                                    void * t116 = gm_l0;
                                    void * t117 = (void *)((uint8_t *)&F.l18);
                                    uint8_t t118 = gm_l2;
                                    gm_tkn42u7na3t81 t119 = gm_l3;
                                    void * t120 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t116, t117, t118, t119);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l19 = t120;
                                    goto gm_lb8;
                                }
gm_lb9:;
                                uint8_t t121 = gm_l2;
                                if (t121) {
                                    void * t122 = gm_l0;
                                    void * t123 = gm_l13;
                                    void * t124 = GM_LD(void *, (((uint8_t *)t123) + 24));
                                    gm_tkn42u7na3t81 t125 = gm_l3;
                                    void * t126 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t122, t124, t125);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l38 = t126;
                                } else {
                                    void * t127 = gm_l0;
                                    void * t128 = gm_l13;
                                    void * t129 = GM_LD(void *, (((uint8_t *)t128) + 24));
                                    void * t130 = gm_f_stmt_expr_evs4ic8((void *)NULL, cx, NULL, t127, t129);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l38 = t130;
                                }
                                void * t131 = gm_l38;
                                gm_l19 = t131;
                            }
gm_lb8:;
                            void * t132 = gm_l0;
                            gm_f_pop_scope_g5j7o4e(t132);
                            void * t133 = (void *)((uint8_t *)&F.l8);
                            void * t134 = gm_l15;
                            void * t135 = gm_l16;
                            void * t136 = gm_l19;
                            gm_tbbqefuhv2r8h t137;
                            GM_ST(void *, ((uint8_t *)&t137 + 0), t134);
                            GM_ST(void *, ((uint8_t *)&t137 + 8), t135);
                            GM_ST(void *, ((uint8_t *)&t137 + 16), t136);
                            gm_tftb1odc739t5 t138 = gm_f_push_bpovr2k_2(t133, &t137);
                            gm_l39 = t138;
                            uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src786i8duem00q, 43, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 27, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t140 = gm_l11;
                uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                uint64_t t143 = r141;
                gm_l11 = t143;
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t144;
        gm_f_list_7s4om10((void *)&t144, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l20 = t144;
        gm_l22 = ((uint64_t)0ULL);
        void * t145 = gm_l1;
        uint64_t t146 = GM_LD(uint64_t, ((((uint8_t *)t145) + 8) + 8));
        gm_l21 = t146;
        {
            for (;;) {
                uint64_t t147 = gm_l22;
                uint64_t t148 = gm_l21;
                uint8_t t149 = (t147 < t148);
                if (!t149) {
                    goto gm_lb10;
                }
                {
                    {
                        {
                            uint64_t t150 = gm_l22;
                            gm_l23 = t150;
                            void * t151 = (void *)((uint8_t *)&F.l20);
                            void * t152 = gm_l1;
                            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 8));
                            uint64_t t154 = GM_LD(uint64_t, ((((uint8_t *)t152) + 8) + 8));
                            uint64_t t155 = gm_l23;
                            if ((uintptr_t)t155 >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)t155, t154, gm_src786i8duem00q, 47, 45);
                            gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t153) + (uintptr_t)t155 * 48) + 32));
                            gm_tftb1odc739t5 t157 = gm_f_push_0mk7udh(t151, GM_CONV(gm_cseahvg8v7p0vs, t156));
                            gm_l40 = t157;
                            uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src786i8duem00q, 47, 30);
                            goto gm_lb13;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 47, 5);
                    }
gm_lb13:;
                }
gm_lb12:;
                uint64_t t159 = gm_l22;
                uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                uint64_t t162 = r160;
                gm_l22 = t162;
            }
        }
gm_lb10:;
        uint8_t t163 = gm_l7;
        if (t163) {
            void * t164 = gm_l0;
            void * t165 = (void *)(((uint8_t *)t164) + 480);
            void * t166 = gm_l1;
            gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 24));
            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
            gm_tbgd14esf0jvi t169;
            GM_ST(uint32_t, (uint8_t *)&t169 + 12, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t169 + 0), t167);
            GM_ST(gm_b16_8, ((uint8_t *)&t169 + 16), t168);
            gm_tftb1odc739t5 t170 = gm_f_push_ltrd0kq(t165, &t169);
            gm_l41 = t170;
            uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
            if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src786i8duem00q, 49, 9);
        } else {
            {
                {
                    void * t172 = gm_l4;
                    gm_t88rcucg1b7uk t173 = gm_l6;
                    uint8_t t174 = gm_f_is_error_8k27gd1(t172, GM_CONV(gm_csguankko4lmhd, t173));
                    uint8_t t175 = (uint8_t)!t174;
                    if (!t175) {
                        goto gm_lb16;
                    }
                    uint8_t t176 = gm_l9;
                    uint8_t t177 = (uint8_t)!t176;
                    if (!t177) {
                        goto gm_lb16;
                    }
                    void * t178 = gm_l0;
                    void * t179 = gm_l0;
                    gm_t88rcucg1b7uk t180 = gm_l6;
                    gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                    gm_tqs4ilbobv1qq t183;
                    gm_f_check_arms_65v4isv((void *)&t183, (void *)NULL, cx, NULL, t179, t180, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t184 = gm_l1;
                    gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 24));
                    gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                    gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                    gm_b0_1 t188 = gm_f_report_match_d77kbfe((void *)NULL, cx, NULL, t178, &t183, t185, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb15;
                }
gm_lb16:;
            }
gm_lb15:;
        }
    }
    {
        void * t189 = gm_l4;
        gm_t88rcucg1b7uk t190 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t190;
        uint8_t t191 = gm_l2;
        if (t191) {
            {
                {
                    uint8_t t192 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                    if (!t192) {
                        goto gm_lb18;
                    }
                    gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                    gm_l25 = t193;
                    gm_t88rcucg1b7uk t194 = gm_l25;
                    gm_l24 = t194;
                    goto gm_lb17;
                }
gm_lb18:;
                gm_trqfvmjtt4sij t195;
                gm_f_list_nof7vfj((void *)&t195, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l26 = t195;
                gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                gm_l27 = t196;
                gm_l28 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t197 = gm_l28;
                        uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                        uint8_t t199 = (t197 < t198);
                        if (!t199) {
                            goto gm_lb19;
                        }
                        {
                            void * t200 = GM_LD(void *, ((uint8_t *)&gm_l27));
                            uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                            uint64_t t202 = gm_l28;
                            {
                                {
                                    memmove(((uint8_t *)&gm_l29), (((uint8_t *)t200) + (uintptr_t)t202 * 24), 24);
                                    void * t203 = (void *)((uint8_t *)&F.l26);
                                    void * t204 = GM_LD(void *, (((uint8_t *)&gm_l29) + 16));
                                    gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t204));
                                    gm_tftb1odc739t5 t206 = gm_f_push_k3l9nic(t203, GM_CONV(gm_csguankko4lmhd, t205));
                                    gm_l42 = t206;
                                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                    if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src786i8duem00q, 59, 35);
                                    goto gm_lb22;
                                }
gm_lb23:;
                                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 59, 13);
                            }
gm_lb22:;
                        }
gm_lb21:;
                        uint64_t t208 = gm_l28;
                        uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                        uint64_t t211 = r209;
                        gm_l28 = t211;
                    }
                }
gm_lb19:;
                void * t212 = gm_l0;
                gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                gm_t88rcucg1b7uk t215 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t212, t214);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t215;
            }
gm_lb17:;
        } else {
            gm_l30 = ((uint8_t)1);
            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_l31 = t216;
            gm_l32 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t217 = gm_l32;
                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                    uint8_t t219 = (t217 < t218);
                    if (!t219) {
                        goto gm_lb24;
                    }
                    {
                        void * t220 = GM_LD(void *, ((uint8_t *)&gm_l31));
                        uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                        uint64_t t222 = gm_l32;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l33), (((uint8_t *)t220) + (uintptr_t)t222 * 24), 24);
                                }
                                {
                                    void * t223 = gm_l4;
                                    void * t224 = GM_LD(void *, (((uint8_t *)&gm_l33) + 16));
                                    gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t224));
                                    gm_t53at80pfdus3 t226;
                                    gm_f_kind_jcnt4hm((void *)&t226, t223, GM_CONV(gm_csguankko4lmhd, t225));
                                    uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&t226) + 0));
                                    uint8_t t228 = (uint8_t)(!(t227 == ((uint8_t)5ULL)));
                                    gm_t53at80pfdus3 t229;
                                    memset(&t229, 0, sizeof t229);
                                    GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)5ULL));
                                    if (t228) {
                                        gm_l30 = ((uint8_t)0);
                                    }
                                }
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 64, 9);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t230 = gm_l32;
                    uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                    uint64_t t233 = r231;
                    gm_l32 = t233;
                }
            }
gm_lb24:;
            uint8_t t234 = gm_l30;
            if (t234) {
                void * t235 = gm_l4;
                gm_t88rcucg1b7uk t236 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t235);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t236;
            }
        }
    }
    {
        {
            {
                uint8_t t237 = gm_l2;
                if (!t237) {
                    goto gm_lb30;
                }
                {
                    void * t238 = gm_l4;
                    gm_t88rcucg1b7uk t239 = gm_l24;
                    gm_t53at80pfdus3 t240;
                    gm_f_kind_jcnt4hm((void *)&t240, t238, GM_CONV(gm_csguankko4lmhd, t239));
                    uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&t240) + 0));
                    uint8_t t242 = (uint8_t)(!(t241 == ((uint8_t)5ULL)));
                    if (!t242) {
                        goto gm_lb30;
                    }
                }
                gm_l35 = ((uint64_t)0ULL);
                uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                gm_l34 = t243;
                {
                    for (;;) {
                        uint64_t t244 = gm_l35;
                        uint64_t t245 = gm_l34;
                        uint8_t t246 = (t244 < t245);
                        if (!t246) {
                            goto gm_lb31;
                        }
                        {
                            {
                                {
                                    uint64_t t247 = gm_l35;
                                    gm_l36 = t247;
                                    void * t248 = GM_LD(void *, ((uint8_t *)&F.l8));
                                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                    uint64_t t250 = gm_l36;
                                    if ((uintptr_t)t250 >= (uintptr_t)(t249)) gm_trap_index((uintptr_t)t250, t249, gm_src786i8duem00q, 71, 18);
                                    void * t251 = GM_LD(void *, ((((uint8_t *)t248) + (uintptr_t)t250 * 24) + 16));
                                    gm_l37 = t251;
                                    {
                                        void * t252 = gm_l4;
                                        void * t253 = gm_l37;
                                        gm_t88rcucg1b7uk t254 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t253));
                                        gm_t53at80pfdus3 t255;
                                        gm_f_kind_jcnt4hm((void *)&t255, t252, GM_CONV(gm_csguankko4lmhd, t254));
                                        uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&t255) + 0));
                                        uint8_t t257 = (uint8_t)(!(t256 == ((uint8_t)5ULL)));
                                        gm_t53at80pfdus3 t258;
                                        memset(&t258, 0, sizeof t258);
                                        GM_ST(uint8_t, ((uint8_t *)&t258 + 0), ((uint8_t)5ULL));
                                        if (t257) {
                                            void * t259 = GM_LD(void *, ((uint8_t *)&F.l8));
                                            uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                            uint64_t t261 = gm_l36;
                                            if ((uintptr_t)t261 >= (uintptr_t)(t260)) gm_trap_index((uintptr_t)t261, t260, gm_src786i8duem00q, 72, 47);
                                            void * t262 = gm_l0;
                                            void * t263 = gm_l37;
                                            gm_t88rcucg1b7uk t264 = gm_l24;
                                            void * t265 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t262, t263, t264);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            GM_ST(void *, ((((uint8_t *)t259) + (uintptr_t)t261 * 24) + 16), t265);
                                        }
                                    }
                                    goto gm_lb34;
                                }
gm_lb35:;
                                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 70, 9);
                            }
gm_lb34:;
                        }
gm_lb33:;
                        uint64_t t266 = gm_l35;
                        uint64_t r267; bool o268 = __builtin_add_overflow(t266, ((uint64_t)1ULL), &r267);
                        uint64_t t269 = r267;
                        gm_l35 = t269;
                    }
                }
gm_lb31:;
                goto gm_lb29;
            }
gm_lb30:;
        }
gm_lb29:;
        gm_t88rcucg1b7uk t270 = gm_l24;
        void * t271 = gm_l5;
        gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        gm_tkaapcii86udk t273;
        GM_ST(void *, ((uint8_t *)&t273 + 0), t271);
        GM_ST(gm_b16_8, ((uint8_t *)&t273 + 8), t272);
        void * t274 = gm_l1;
        gm_tl874t2atnlh3 t275 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t274) + 24));
        gm_to0f5qn75cgg8 t276;
        memset(&t276, 0, sizeof t276);
        GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t276 + 8), t273);
        void * t277 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t270, &t276, t275);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t277;
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
inline __attribute__((aligned(4))) void gm_f_nested_pattern_oiclpgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_nested_pattern_oiclpgf F;
    void * gm_l0; /* e */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            {
                void * t4 = gm_l0;
                uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
                uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
                if (!t6) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l1), ((((uint8_t *)t4) + 8) + 8), 32);
            }
            {
                void * t7 = gm_l0;
                uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)t7));
                gm_t08olq97rscn3 t9 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l1));
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
                gm_to361lgknsu69 t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), t9);
                gm_t0d02142k4tu8 t13;
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 60, 0);
                GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t8);
                GM_ST(gm_to361lgknsu69, ((uint8_t *)&t13 + 8), t12);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13 + 48), t11);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
                GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out)), t13);
                ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_f_expr_to_pattern_rchr7ig((void *)&(*(gm_toqf51949ppls *)gm_out), (void *)NULL, cx, NULL, t14);
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
__attribute__((aligned(4))) uint8_t gm_f_omits_payload_e33pdef(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_omits_payload_e33pdef F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    void * gm_l5; /* inner */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* i */
    gm_t88rcucg1b7uk gm_l8; /* err */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_tagc77imt2chm gm_l10; /* pp */
    gm_t08olq97rscn3 gm_l11; /* n */
    gm_t08olq97rscn3 gm_l12; /* n */
    gm_b16_8 gm_l13; /* name */
    gm_tep2drsl9408b gm_l14; /* uv */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l18;
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_tdlpv0q69lov1 gm_l21;
    uint8_t gm_l22;
    void * t28 = gm_l0;
    void * t29 = GM_LD(void *, ((uint8_t *)t28));
    void * t30 = (void *)(((uint8_t *)t29) + 8);
    gm_l3 = t30;
    void * t31 = gm_l0;
    void * t32 = (void *)(((uint8_t *)t31) + 96);
    void * t33 = gm_l0;
    void * t34 = GM_LD(void *, ((uint8_t *)t33));
    gm_t88rcucg1b7uk t35 = gm_l2;
    gm_t88rcucg1b7uk t36 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t32, t34, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t36;
    {
        {
            void * t37 = gm_l1;
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)6ULL));
            if (!t39) {
                goto gm_lb2;
            }
            void * t40 = GM_LD(void *, ((((uint8_t *)t37) + 8) + 8));
            gm_l5 = t40;
            gm_t88rcucg1b7uk t41 = gm_l4;
            gm_l6 = t41;
            {
                for (;;) {
                    void * t42 = gm_l3;
                    gm_t88rcucg1b7uk t43 = gm_l6;
                    gm_f_kind_jcnt4hm((void *)&gm_l18, t42, GM_CONV(gm_csguankko4lmhd, t43));
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)13ULL));
                    if (!t45) {
                        goto gm_lb3;
                    }
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
                    gm_l7 = t46;
                    {
                        gm_t88rcucg1b7uk t47 = gm_l7;
                        gm_l6 = t47;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            {
                {
                    void * t48 = gm_l3;
                    gm_t88rcucg1b7uk t49 = gm_l6;
                    gm_f_kind_jcnt4hm((void *)&gm_l19, t48, GM_CONV(gm_csguankko4lmhd, t49));
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)14ULL));
                    if (!t51) {
                        goto gm_lb7;
                    }
                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
                    gm_l8 = t52;
                    void * t53 = gm_l0;
                    void * t54 = gm_l5;
                    gm_t88rcucg1b7uk t55 = gm_l8;
                    uint8_t t56 = gm_f_omits_payload_e33pdef((void *)NULL, cx, NULL, t53, t54, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t56;
                    goto gm_fret;
                }
gm_lb7:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb6:;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t57 = gm_l0;
    gm_t88rcucg1b7uk t58 = gm_l4;
    gm_t88rcucg1b7uk t59 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t57, GM_CONV(gm_csguankko4lmhd, t58)));
    gm_l9 = t59;
    {
        {
            {
                void * t60 = gm_l1;
                uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)7ULL));
                if (!t62) {
                    goto gm_lb9;
                }
                memmove(((uint8_t *)&gm_l10), ((((uint8_t *)t60) + 8) + 8), 32);
            }
            {
                {
                    uint8_t t63 = (uint8_t)(((((uint8_t *)&gm_l10) + 8))[16] != 0);
                    if (t63) {
                        goto gm_lb11;
                    }
                    gm_l20 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l20 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 243, 8);
            }
gm_lb10:;
            uint8_t t64 = gm_l20;
            if (t64) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t65 = GM_LD(void *, ((uint8_t *)&gm_l10));
            {
                {
                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)8ULL));
                    if (!t67) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&gm_l11), ((((uint8_t *)t65) + 8) + 8), 32);
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                    gm_l13 = t68;
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)22ULL));
                    if (!t70) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&gm_l12), (((((uint8_t *)t65) + 8) + 8) + 8), 32);
                    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                    gm_l13 = t71;
                    goto gm_lb13;
                }
gm_lb15:;
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 244, 13);
            }
gm_lb13:;
            {
                void * t72 = gm_l0;
                void * t73 = GM_LD(void *, ((uint8_t *)t72));
                gm_t88rcucg1b7uk t74 = gm_l9;
                gm_f_union_variants_a7khaqp((void *)&gm_l21, t73, GM_CONV(gm_csguankko4lmhd, t74));
            }
            uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l21))[24] != 0);
            if (t75) {
                memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l21), 24);
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
            gm_l15 = t76;
            gm_l16 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t77 = gm_l16;
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint8_t t79 = (t77 < t78);
                    if (!t79) {
                        goto gm_lb17;
                    }
                    {
                        void * t80 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t82 = gm_l16;
                        {
                            {
                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t80) + (uintptr_t)t82 * 72), 72);
                                {
                                    {
                                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                        gm_b16_8 t84 = gm_l13;
                                        uint8_t t85 = (gm_eq0iqiubfm7e(((uint8_t *)&t83), ((uint8_t *)&t84)));
                                        if (!t85) {
                                            goto gm_lb23;
                                        }
                                        {
                                            {
                                                uint8_t t86 = (uint8_t)(((((uint8_t *)&F.l17) + 16))[4] != 0);
                                                if (t86) {
                                                    goto gm_lb25;
                                                }
                                                gm_l22 = ((uint8_t)0);
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                            {
                                                gm_l22 = ((uint8_t)1);
                                                goto gm_lb24;
                                            }
gm_lb26:;
                                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 251, 31);
                                        }
gm_lb24:;
                                        uint8_t t87 = gm_l22;
                                        if (!t87) {
                                            goto gm_lb23;
                                        }
                                        F.ret = ((uint8_t)1);
                                        goto gm_fret;
                                    }
gm_lb23:;
                                }
gm_lb22:;
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 250, 5);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t88 = gm_l16;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                    uint64_t t91 = r89;
                    gm_l16 = t91;
                }
            }
gm_lb17:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb9:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb8:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_remap_binds_kao301q(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remap_binds_kao301q F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l5; /* m */
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    void * t10 = gm_l0;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t10) + 8) + 8));
            gm_l2 = t13;
            gm_b16_8 t14 = F.l1;
            gm_l3 = t14;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t18 = (t16 < t17);
                    if (!t18) {
                        goto gm_lb3;
                    }
                    {
                        void * t19 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t21 = gm_l4;
                        {
                            {
                                gm_tcc1rq4n1avb2 t22 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t19) + (uintptr_t)t21 * 8));
                                gm_l5 = t22;
                                uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                                uint32_t t24 = gm_l2;
                                uint8_t t25 = ((t23 == t24));
                                if (t25) {
                                    void * t26 = gm_l0;
                                    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                    gm_tb38v5s9j1r32 t28;
                                    memset(&t28, 0, sizeof t28);
                                    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
                                    GM_ST(gm_tb38v5s9j1r32, (((uint8_t *)t26) + 8), t28);
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 167, 22);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            void * t33 = gm_l0;
            gm_closure t34 = ((gm_closure){(void *)gm_f_fn_ho0p9pu, (void *)&F.h, cx});
            gm_f_for_each_subpat_arlrtrm(t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 166, 5);
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_match_d77kbfe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3) {
    struct gm_fr_report_match_d77kbfe F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* arms */
    gm_b16_8 gm_l4; /* missing */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l10;
    {
        {
            uint8_t t11 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l4 = t12;
            {
                gm_tl874t2atnlh3 t13 = gm_l2;
                gm_b16_8 t14 = gm_l4;
                gm_tnpifv20irnph t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
                gm_f_format_d277g96((void *)&gm_l10, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src786i8duem00q, 90, 31);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_t4gut2c9fj1kt t19;
                gm_f_error_s6hn871((void *)&t19, (void *)NULL, cx, NULL, t13, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t19;
            }
            {
                {
                    uint8_t t20 = (uint8_t)(((((uint8_t *)a1) + 40))[12] != 0);
                    if (!t20) {
                        goto gm_lb4;
                    }
                    gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 40));
                    gm_l6 = t21;
                    void * t22 = (void *)((uint8_t *)&F.l5);
                    gm_tl874t2atnlh3 t23 = gm_l6;
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s3ceboq671qr5));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(38));
                    gm_f_add_label_50fj7vv(t22, GM_CONV(gm_cseahvg8v7p0vs, t23), t24);
                    void * t25 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s67h6d8kdg7qc));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(166));
                    gm_f_add_note_46v502s(t25, t26);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                void * t27 = gm_l0;
                void * t28 = GM_LD(void *, ((uint8_t *)t27));
                gm_b0_1 t29 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t28, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    gm_l7 = t30;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l8;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb5;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t36 = gm_l8;
                {
                    {
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t34) + (uintptr_t)t36 * 8));
                        gm_l9 = t37;
                        void * t38 = gm_l0;
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t41 = gm_l9;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_src786i8duem00q, 97, 44);
                        gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + (uintptr_t)t41 * 12));
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s5p756i54h2sb));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(51));
                        gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t38, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 97, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t45 = gm_l8;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l8 = t48;
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

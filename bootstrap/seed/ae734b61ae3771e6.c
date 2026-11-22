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
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
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
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_t5v0c8ia6a5mo f0; uint8_t f1; uint8_t pe[7]; } gm_tj8m6gmlgjilo;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t6ok5tvcjc200 f1; } gm_t70bah4iduq3d;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj8m6gmlgjilo) == 64 && _Alignof(gm_tj8m6gmlgjilo) == 8 && offsetof(gm_tj8m6gmlgjilo, f0) == 0 && offsetof(gm_tj8m6gmlgjilo, f1) == 56, "layout of `?ParamInfo`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70bah4iduq3d) == 48 && _Alignof(gm_t70bah4iduq3d) == 8 && offsetof(gm_t70bah4iduq3d, f0) == 0 && offsetof(gm_t70bah4iduq3d, f1) == 8, "layout of `(kind: Kind, hover: Hover)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t2gtbh39ptekd f0; uint8_t f1; uint8_t pe[7]; } gm_t89nb7ept390i;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t89nb7ept390i) == 72 && _Alignof(gm_t89nb7ept390i) == 8 && offsetof(gm_t89nb7ept390i, f0) == 0 && offsetof(gm_t89nb7ept390i, f1) == 64, "layout of `?ExprKind`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t6qooom8qa2do f0; uint8_t f1; uint8_t pe[7]; } gm_tlabdfckddbfd;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tlabdfckddbfd) == 216 && _Alignof(gm_tlabdfckddbfd) == 8 && offsetof(gm_tlabdfckddbfd, f0) == 0 && offsetof(gm_tlabdfckddbfd, f1) == 208, "layout of `?DeclValue`");
struct gm_fr_adt_decl_of_2bflsov {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_builtin_methods_st9oapf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_decl_done_mk70ol9 {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_tlabdfckddbfd ret;
};
struct gm_fr_decl_info_ahk1bni {
    gm_frame h;
    gm_t89nb7ept390i l6; /* init_kind */
    gm_tv2uf4leil5hq l7; /* b */
    gm_tlabdfckddbfd l10; /* value */
    gm_t6qooom8qa2do l12; /* v */
    gm_t2gtbh39ptekd l16; /* k */
    gm_t70bah4iduq3d ret;
};
struct gm_fr_decl_item_a57s8if {
    gm_frame h;
    gm_t70bah4iduq3d l3; /* info */
    gm_t6qooom8qa2do l5; /* v */
    gm_tl5cuhjfnc4cr ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_items_9bsf0mi {
    gm_frame h;
    gm_tpautr4br7h3l l6; /* def */
    gm_t0mglksrkfa5e l12; /* f */
    gm_b0_1 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_first_line_jto2r5e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* lines */
    gm_b16_8 ret;
};
struct gm_fr_fn_6gkpg0v_2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_signature_urdu16i {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* params */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l13; /* out */
    gm_b16_8 ret;
};
struct gm_fr_format_0555s29 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_22ud8a7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_279asi0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_407qqln {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9ndqnou {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6rro2t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hb6cr3p {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vfnel93 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_is_core_file_4u6phfn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_text_bv1q6fm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* lines */
    gm_trqfvmjtt4sij l4; /* out */
    gm_b16_8 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lines_of_ai9428u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_members_26b4s91 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* ds */
    gm_trqfvmjtt4sij l7; /* items */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_module_scope_items_9t7jui3 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* ds */
    gm_trqfvmjtt4sij l6; /* items */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_op_info_4a09t36 {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_t6qooom8qa2do l9; /* v */
    gm_trqfvmjtt4sij l13; /* ps */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t70bah4iduq3d ret;
};
struct gm_fr_op_item_8dfr8db {
    gm_frame h;
    gm_t70bah4iduq3d l3; /* info */
    gm_tv2uf4leil5hq l6; /* b */
    gm_tl5cuhjfnc4cr ret;
};
struct gm_fr_operation_members_62l824c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_t6qooom8qa2do l3; /* v */
    gm_tv0k2vg3054a6 l8; /* op */
    gm_trqfvmjtt4sij l9; /* ps */
    gm_t5v0c8ia6a5mo l12; /* p */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_1e500nc_3 {
    gm_frame h;
    gm_tl5cuhjfnc4cr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sort_o401rjn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_src_fjcc76p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_universe_hidden_gsk2b8r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_universe_items_235nbu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_trqfvmjtt4sij l3; /* ds */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_value_members_03rtuq5 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_trqfvmjtt4sij l6; /* ds */
    gm_t6qooom8qa2do l13; /* v */
    gm_tj8m6gmlgjilo l16; /* first */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_t5v0c8ia6a5mo l20; /* p */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_variant_members_nesi2hv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tckdobunaulee l8; /* v */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_write_aisrlpj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_clopc8o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_hnivvga {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_builtin_methods_st9oapf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_item_a57s8if(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_3(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_field_items_9bsf0mi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, uint32_t a3);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_first_line_jto2r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_signature_urdu16i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_format_0555s29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_22ud8a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_279asi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_9ndqnou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hb6cr3p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jfe99lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_vfnel93(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_core_file_4u6phfn(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_item_text_bv1q6fm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lines_of_ai9428u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_module_scope_items_9t7jui3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_op_item_8dfr8db(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_universe_items_235nbu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_members_03rtuq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aisrlpj(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_clopc8o(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hnivvga(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
extern uint8_t gm_c8g6p8jp14b[96] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
extern uint8_t gm_cjfp4khah06[80] __attribute__((aligned(8)));
extern uint8_t gm_cjts5fiuldu[192] __attribute__((aligned(8)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern const uint8_t gm_s1dmrsprfdp53[8];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s23ifilt9knnp[18];
extern const uint8_t gm_s2nf76g2su2k3[15];
extern const uint8_t gm_s3s4n6ehcrs2b[17];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sjjehkhgpkn3v[10];
extern const uint8_t gm_sjm2j40p6489d[9];
extern const uint8_t gm_sjsv6e8j9nikr[7];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sk8fkllqjst4j[13];
extern const uint8_t gm_slqgeomhftr16[15];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srcm0juv8ecdls4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ssd7gce6jed13[7];
extern const uint8_t gm_st2a9sgu5aria[11];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const gm_tmpl gm_tpl0aluqdlmv333;
extern const gm_tmpl gm_tpl37uc86rluksa;
extern const gm_tmpl gm_tpl3c9lssf4k186;
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const gm_tmpl gm_tpl5r25bpo8hobt;
extern const gm_tmpl gm_tpl5s6q12qjusi2;
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const gm_tmpl gm_tplrfnb607u5hvd;
extern const gm_tmpl gm_tplvkpfj3qq0aig;
extern const gm_tmpl gm_tplvpehcchgo971;
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 190, 14);
    void * t7 = (void *)(((uint8_t *)t4) + (uintptr_t)t6 * 216);
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
    void * t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
    uint64_t t13 = gm_l2;
    uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
    F.ret = t14;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_3 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v_2 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    void * t4 = gm_l1;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)t4));
    uint8_t t6 = (t3 < t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0555s29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0555s29 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssd7gce6jed13));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvkpfj3qq0aig), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_22ud8a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_22ud8a7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjtp8ip8t28uh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqt9e8rihgogv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_279asi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_279asi0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1dmrsprfdp53));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5r25bpo8hobt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_407qqln F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd9r0l17bbuoe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvpehcchgo971), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9ndqnou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9ndqnou F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s23ifilt9knnp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_jfe99lk((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5s6q12qjusi2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hb6cr3p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hb6cr3p F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slqgeomhftr16));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0aluqdlmv333), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vfnel93(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vfnel93 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjm2j40p6489d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrfnb607u5hvd), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)12ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpi80k4_3();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_3 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4_3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o86m6d7_3(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_73v4ttb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k4j2gj3(t8, ((uint64_t)1ULL), t10);
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
    uint64_t t18 = gm_l1;
    GM_ST(uint64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_o401rjn F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v_2), NULL, cx});
    gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_universe_hidden_gsk2b8r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 67, 64);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t4) + (uintptr_t)t6 * 216));
    void * t8 = (void *)((uint8_t *)gm_cfiqcgtjhen);
    uint64_t t9 = ((uintptr_t)((uint64_t)26ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
    uint8_t t11 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 16, ((uint8_t *)&t7))) { t11 = 1; break; }
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aisrlpj(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_aisrlpj F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjjehkhgpkn3v));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl37uc86rluksa), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_clopc8o(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_clopc8o F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ssd7gce6jed13));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvkpfj3qq0aig), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hnivvga(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_hnivvga F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjsv6e8j9nikr));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tpl3c9lssf4k186), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_decl_of_2bflsov F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty_in */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l5;
    gm_t53at80pfdus3 gm_l6;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_l2 = t7;
    {
        for (;;) {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)8ULL));
            if (!t12) {
                goto gm_lb1;
            }
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l3 = t13;
            {
                gm_t88rcucg1b7uk t14 = gm_l3;
                gm_l2 = t14;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t15 = gm_l0;
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l6, t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)17ULL));
            if (!t19) {
                goto gm_lb5;
            }
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t20;
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1088));
            uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1088) + 8));
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            uint32_t t26 = gm_l4;
            uint32_t t27 = gm_f_template_9o92cvv(t25, t26);
            uint64_t t28 = (uint64_t)t27;
            if ((uintptr_t)t28 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t28, t23, gm_srcm0juv8ecdls4, 362, 12);
            gm_tf5srh4mp4b3i t29 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t22) + (uintptr_t)t28 * 184) + 168));
            F.ret = t29;
            goto gm_fret;
        }
gm_lb5:;
        gm_tf5srh4mp4b3i t30;
        memset(&t30, 0, sizeof t30);
        F.ret = t30;
        goto gm_fret;
    }
gm_lb4:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_builtin_methods_st9oapf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_builtin_methods_st9oapf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
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
                        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                        gm_l4 = t14;
                        {
                            void * t15 = gm_l1;
                            gm_b16_8 t16 = gm_l4;
                            gm_b16_8 t17;
                            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2nf76g2su2k3));
                            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(14));
                            gm_th2lqbo77pvsc t18;
                            memset(&t18, 0, sizeof t18);
                            gm_t9qkh0rsooc8d t19;
                            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)2ULL));
                            gm_tl5cuhjfnc4cr t20;
                            GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t16);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 16), t19);
                            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 24), t17);
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t20 + 40), t18);
                            gm_tftb1odc739t5 t21 = gm_f_push_1e500nc_3(t15, &t20);
                            gm_l5 = t21;
                        }
                        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcm0juv8ecdls4, 269, 22);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 269, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l3;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l3 = t26;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_done_mk70ol9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 458u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tagrkbqh054h7 gm_l3;
    {
        {
            {
                void * t4 = gm_l0;
                uint64_t t5 = gm_l1;
                gm_f_state_a5bnsjb((void *)&gm_l3, (void *)NULL, cx, NULL, t4, t5);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)2ULL));
            if (!t7) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 208);
            }
            {
                gm_t6qooom8qa2do t8 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&F.l2));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 212, 0);
                GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out)), t8);
                ((uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out))[208] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    memset(&(*(gm_tlabdfckddbfd *)gm_out), 0, sizeof (*(gm_tlabdfckddbfd *)gm_out));
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_info_ahk1bni F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    gm_th2lqbo77pvsc gm_l4; /* doc */
    void * gm_l5; /* item */
    gm_t9qkh0rsooc8d gm_l8; /* kind */
    gm_b16_8 gm_l9; /* code */
    uint8_t gm_l11; /* handled */
    uint32_t gm_l13; /* f */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_t88rcucg1b7uk gm_l15; /* ty */
    GM_STACK_CHECK(&F, 1112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_t9qkh0rsooc8d gm_l20;
    gm_b16_8 gm_l21;
    {
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, ((uint8_t *)t38));
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 240));
        uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 240) + 8));
        uint64_t t42 = gm_l1;
        if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcm0juv8ecdls4, 130, 14);
        void * t43 = (void *)(((uint8_t *)t40) + (uintptr_t)t42 * 216);
        gm_l2 = t43;
        void * t44 = gm_l2;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)t44));
        gm_l3 = t45;
        void * t46 = gm_l2;
        memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t46) + 40) + 16), 24);
        void * t47 = gm_l2;
        void * t48 = (void *)(((uint8_t *)t47) + 40);
        gm_l5 = t48;
        memset(&F.l6, 0, sizeof F.l6);
        {
            {
                {
                    void * t49 = gm_l5;
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t49) + 64) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
                    if (!t51) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l7), ((((uint8_t *)t49) + 64) + 8), 64);
                }
                {
                    void * t52 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                    gm_t2gtbh39ptekd t53 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t52) + 8));
                    GM_ST(uint8_t, (uint8_t *)&F.l6 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l6 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l6 + 68, 0);
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&F.l6), t53);
                    ((uint8_t *)&F.l6)[64] = 1;
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_t9qkh0rsooc8d t54;
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)7ULL));
        gm_l8 = t54;
        gm_b16_8 t55 = gm_l3;
        gm_l9 = t55;
        void * t56 = gm_l0;
        uint64_t t57 = gm_l1;
        gm_f_decl_done_mk70ol9((void *)&F.l10, (void *)NULL, cx, NULL, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = ((uint8_t)1);
        {
            {
                uint8_t t58 = (uint8_t)((((uint8_t *)&F.l10))[208] != 0);
                if (!t58) {
                    goto gm_lb4;
                }
                memmove(((uint8_t *)&F.l12), ((uint8_t *)&F.l10), 208);
                {
                    {
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t60 = (uint8_t)(t59 == ((uint8_t)5ULL));
                        if (!t60) {
                            goto gm_lb6;
                        }
                        uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&F.l12) + 8));
                        gm_l13 = t61;
                        gm_t9qkh0rsooc8d t62;
                        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)1ULL));
                        gm_l8 = t62;
                        {
                            gm_b16_8 t63 = gm_l3;
                            void * t64 = gm_l0;
                            uint32_t t65 = gm_l13;
                            gm_b16_8 t66 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t64, t65, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t67;
                            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t63);
                            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t66);
                            gm_f_format_vfnel93((void *)&gm_l17, (void *)NULL, cx, NULL, &t67);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcm0juv8ecdls4, 144, 24);
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                        gm_l9 = t70;
                        goto gm_lb5;
                    }
gm_lb6:;
                    {
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)7ULL));
                        if (!t72) {
                            goto gm_lb7;
                        }
                        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 8));
                        gm_l14 = t73;
                        gm_t9qkh0rsooc8d t74;
                        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)7ULL));
                        gm_l8 = t74;
                        {
                            gm_b16_8 t75 = gm_l3;
                            void * t76 = gm_l0;
                            gm_t88rcucg1b7uk t77 = gm_l14;
                            gm_b16_8 t78 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t76, t77);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t79;
                            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t75);
                            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 16), t78);
                            gm_f_format_f6rro2t((void *)&gm_l18, (void *)NULL, cx, NULL, &t79);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcm0juv8ecdls4, 148, 24);
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                        gm_l9 = t82;
                        goto gm_lb5;
                    }
gm_lb7:;
                    {
                        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t84 = (uint8_t)(t83 == ((uint8_t)10ULL));
                        if (!t84) {
                            goto gm_lb8;
                        }
                        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 8));
                        gm_l15 = t85;
                        gm_t9qkh0rsooc8d t86;
                        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)8ULL));
                        gm_l8 = t86;
                        {
                            gm_b16_8 t87 = gm_l3;
                            void * t88 = gm_l0;
                            gm_t88rcucg1b7uk t89 = gm_l15;
                            gm_b16_8 t90 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t88, t89);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t91;
                            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t87);
                            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t90);
                            gm_f_format_hb6cr3p((void *)&gm_l19, (void *)NULL, cx, NULL, &t91);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcm0juv8ecdls4, 152, 24);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                        gm_l9 = t94;
                        goto gm_lb5;
                    }
gm_lb8:;
                    {
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)9ULL));
                        if (!t96) {
                            goto gm_lb9;
                        }
                        gm_t9qkh0rsooc8d t97;
                        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
                        gm_l8 = t97;
                        void * t98 = gm_l0;
                        void * t99 = gm_l5;
                        gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t99) + 4));
                        gm_b16_8 t101 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t98, t100);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t101;
                        goto gm_lb5;
                    }
gm_lb9:;
                    {
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t103 = (uint8_t)(t102 == ((uint8_t)2ULL));
                        if (!t103) {
                            goto gm_lb10;
                        }
                        gm_t9qkh0rsooc8d t104;
                        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)5ULL));
                        gm_l8 = t104;
                        void * t105 = gm_l0;
                        void * t106 = gm_l5;
                        gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 4));
                        gm_b16_8 t108 = gm_f_item_text_bv1q6fm((void *)NULL, cx, NULL, t105, t107);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t108;
                        goto gm_lb5;
                    }
gm_lb10:;
                    {
                        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                        uint8_t t110 = (uint8_t)(t109 == ((uint8_t)8ULL));
                        if (!t110) {
                            goto gm_lb11;
                        }
                        gm_t9qkh0rsooc8d t111;
                        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)7ULL));
                        gm_l8 = t111;
                        void * t112 = gm_l0;
                        void * t113 = gm_l5;
                        gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 4));
                        gm_b16_8 t115 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t112, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t115;
                        goto gm_lb5;
                    }
gm_lb11:;
                    {
                        {
                            {
                                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)0ULL));
                                if (!t117) {
                                    goto gm_lb14;
                                }
                                goto gm_lb13;
                            }
gm_lb14:;
                            {
                                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                uint8_t t119 = (uint8_t)(t118 == ((uint8_t)1ULL));
                                if (!t119) {
                                    goto gm_lb15;
                                }
                                goto gm_lb13;
                            }
gm_lb15:;
                            {
                                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                uint8_t t121 = (uint8_t)(t120 == ((uint8_t)11ULL));
                                if (!t121) {
                                    goto gm_lb16;
                                }
                                goto gm_lb13;
                            }
gm_lb16:;
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_l11 = ((uint8_t)0);
                        goto gm_lb5;
                    }
gm_lb12:;
                    {
                        goto gm_lb5;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 141, 9);
                }
gm_lb5:;
                goto gm_lb3;
            }
gm_lb4:;
            gm_l11 = ((uint8_t)0);
        }
gm_lb3:;
    }
    {
        uint8_t t122 = gm_l11;
        uint8_t t123 = (uint8_t)!t122;
        if (t123) {
            gm_t9qkh0rsooc8d t124;
            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)14ULL));
            gm_l8 = t124;
            {
                {
                    uint8_t t125 = (uint8_t)((((uint8_t *)&F.l6))[64] != 0);
                    if (!t125) {
                        goto gm_lb19;
                    }
                    memmove(((uint8_t *)&F.l16), ((uint8_t *)&F.l6), 64);
                    {
                        {
                            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)32ULL));
                            if (!t127) {
                                goto gm_lb21;
                            }
                            gm_t9qkh0rsooc8d t128;
                            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)3ULL));
                            gm_l20 = t128;
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)33ULL));
                            if (!t130) {
                                goto gm_lb22;
                            }
                            gm_t9qkh0rsooc8d t131;
                            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)4ULL));
                            gm_l20 = t131;
                            goto gm_lb20;
                        }
gm_lb22:;
                        {
                            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            uint8_t t133 = (uint8_t)(t132 == ((uint8_t)34ULL));
                            if (!t133) {
                                goto gm_lb23;
                            }
                            gm_t9qkh0rsooc8d t134;
                            GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)5ULL));
                            gm_l20 = t134;
                            goto gm_lb20;
                        }
gm_lb23:;
                        {
                            {
                                {
                                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                    uint8_t t136 = (uint8_t)(t135 == ((uint8_t)38ULL));
                                    if (!t136) {
                                        goto gm_lb26;
                                    }
                                    goto gm_lb25;
                                }
gm_lb26:;
                                {
                                    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                    uint8_t t138 = (uint8_t)(t137 == ((uint8_t)39ULL));
                                    if (!t138) {
                                        goto gm_lb27;
                                    }
                                    goto gm_lb25;
                                }
gm_lb27:;
                                goto gm_lb24;
                            }
gm_lb25:;
                            gm_t9qkh0rsooc8d t139;
                            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)0ULL));
                            gm_l20 = t139;
                            goto gm_lb20;
                        }
gm_lb24:;
                        {
                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)36ULL));
                            if (!t141) {
                                goto gm_lb28;
                            }
                            gm_t9qkh0rsooc8d t142;
                            GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)1ULL));
                            gm_l20 = t142;
                            goto gm_lb20;
                        }
gm_lb28:;
                        {
                            gm_t9qkh0rsooc8d t143;
                            GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)14ULL));
                            gm_l20 = t143;
                            goto gm_lb20;
                        }
gm_lb29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 175, 20);
                    }
gm_lb20:;
                    gm_t9qkh0rsooc8d t144 = gm_l20;
                    gm_l8 = t144;
                    goto gm_lb18;
                }
gm_lb19:;
            }
gm_lb18:;
            {
                {
                    {
                        {
                            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t146 = (uint8_t)(t145 == ((uint8_t)3ULL));
                            if (!t146) {
                                goto gm_lb33;
                            }
                            goto gm_lb32;
                        }
gm_lb33:;
                        {
                            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)4ULL));
                            if (!t148) {
                                goto gm_lb34;
                            }
                            goto gm_lb32;
                        }
gm_lb34:;
                        {
                            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t150 = (uint8_t)(t149 == ((uint8_t)5ULL));
                            if (!t150) {
                                goto gm_lb35;
                            }
                            goto gm_lb32;
                        }
gm_lb35:;
                        goto gm_lb31;
                    }
gm_lb32:;
                    void * t151 = gm_l0;
                    void * t152 = gm_l5;
                    gm_tl874t2atnlh3 t153 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t152) + 4));
                    gm_b16_8 t154 = gm_f_item_text_bv1q6fm((void *)NULL, cx, NULL, t151, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t154;
                    goto gm_lb30;
                }
gm_lb31:;
                {
                    void * t155 = gm_l0;
                    void * t156 = gm_l5;
                    gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 4));
                    gm_b16_8 t158 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t155, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t158;
                    goto gm_lb30;
                }
gm_lb36:;
                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 184, 16);
            }
gm_lb30:;
            gm_b16_8 t159 = gm_l21;
            gm_l9 = t159;
        }
    }
    {
        gm_t9qkh0rsooc8d t160 = gm_l8;
        gm_b16_8 t161 = gm_l9;
        gm_th2lqbo77pvsc t162 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
        gm_t6ok5tvcjc200 t163;
        GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t161);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t163 + 16), t162);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 0), t160);
        GM_ST(gm_t6ok5tvcjc200, ((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 8), t163);
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
inline __attribute__((aligned(4))) void gm_f_decl_item_a57s8if(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_item_a57s8if F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    gm_b16_8 gm_l4; /* detail */
    uint32_t gm_l6; /* f */
    gm_t88rcucg1b7uk gm_l7; /* ty */
    gm_t88rcucg1b7uk gm_l8; /* ty */
    GM_STACK_CHECK(&F, 972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlabdfckddbfd gm_l9;
    gm_b16_8 gm_l10;
    gm_trqfvmjtt4sij gm_l11;
    gm_b16_8 gm_l12;
    gm_th2lqbo77pvsc gm_l13;
    gm_th2lqbo77pvsc gm_l14;
    gm_b16_8 gm_l15;
    gm_trqfvmjtt4sij gm_l16;
    gm_b16_8 gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 240));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 240) + 8));
    uint64_t t15 = gm_l1;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcm0juv8ecdls4, 194, 14);
    void * t16 = (void *)(((uint8_t *)t13) + (uintptr_t)t15 * 216);
    gm_l2 = t16;
    {
        void * t17 = gm_l0;
        uint64_t t18 = gm_l1;
        gm_f_decl_info_ahk1bni((void *)&F.l3, (void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
    gm_l4 = t19;
    {
        {
            {
                void * t20 = gm_l0;
                uint64_t t21 = gm_l1;
                gm_f_decl_done_mk70ol9((void *)&gm_l9, (void *)NULL, cx, NULL, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l9))[208] != 0);
            if (!t22) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l9), 208);
            }
            {
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)5ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
                    gm_l6 = t25;
                    void * t26 = gm_l0;
                    uint32_t t27 = gm_l6;
                    gm_b16_8 t28 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t26, t27, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l4 = t28;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        {
                            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)7ULL));
                            if (!t30) {
                                goto gm_lb7;
                            }
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l7 = t31;
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)10ULL));
                            if (!t33) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                            gm_l7 = t34;
                            goto gm_lb6;
                        }
gm_lb8:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    void * t35 = gm_l0;
                    gm_t88rcucg1b7uk t36 = gm_l7;
                    gm_b16_8 t37 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t35, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l4 = t37;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    {
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                        gm_f_lines_of_ai9428u((void *)&gm_l11, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                    gm_l12 = t39;
                    void * t40 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t42 = (((uint64_t)0ULL) < t41);
                    if (t42) {
                        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t40) + (uintptr_t)((uint64_t)0ULL) * 16));
                        GM_ST(uint8_t, (uint8_t *)&gm_l13 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l13 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l13 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l13), t43);
                        ((uint8_t *)&gm_l13)[16] = 1;
                    } else {
                        memset(&gm_l13, 0, sizeof gm_l13);
                    }
                    {
                        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 24);
                    }
                    uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                    if (t44) {
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                        gm_l10 = t45;
                    } else {
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
                        gm_l10 = t46;
                    }
                    gm_b16_8 t47 = gm_l10;
                    gm_l4 = t47;
                    goto gm_lb3;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 198, 9);
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_f_lines_of_ai9428u((void *)&gm_l16, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
        gm_l17 = t49;
        void * t50 = GM_LD(void *, ((uint8_t *)&gm_l17));
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
        uint8_t t52 = (((uint64_t)0ULL) < t51);
        if (t52) {
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t50) + (uintptr_t)((uint64_t)0ULL) * 16));
            GM_ST(uint8_t, (uint8_t *)&gm_l18 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l18 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l18 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l18), t53);
            ((uint8_t *)&gm_l18)[16] = 1;
        } else {
            memset(&gm_l18, 0, sizeof gm_l18);
        }
        {
            memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 24);
        }
        uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
        if (t54) {
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
            gm_l15 = t55;
        } else {
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(0));
            gm_l15 = t56;
        }
        gm_b16_8 t57 = gm_l15;
        gm_l4 = t57;
    }
gm_lb1:;
    {
        void * t58 = gm_l2;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)t58));
        gm_t9qkh0rsooc8d t60 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l3));
        gm_b16_8 t61 = gm_l4;
        gm_th2lqbo77pvsc t62 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)&F.l3) + 8) + 16));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 0), t59);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 16), t60);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 24), t61);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 40), t62);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_field_items_9bsf0mi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, uint32_t a3) {
    struct gm_fr_field_items_9bsf0mi F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* items */
    uint32_t gm_l3; /* depth */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* info */
    gm_b16_8 gm_l7; /* fs */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    GM_STACK_CHECK(&F, 694u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l13;
    gm_t8ta482bl6be0 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    {
        {
            void * t15 = gm_l0;
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l13, t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)17ULL));
            if (!t19) {
                goto gm_lb2;
            }
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
            gm_l4 = t20;
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1088));
            uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1088) + 8));
            uint32_t t24 = gm_l4;
            uint64_t t25 = (uint64_t)t24;
            if ((uintptr_t)t25 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t25, t23, gm_srcm0juv8ecdls4, 316, 14);
            void * t26 = (void *)(((uint8_t *)t22) + (uintptr_t)t25 * 184);
            gm_l5 = t26;
            void * t27 = gm_l5;
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
                    if (!t29) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l6), ((((uint8_t *)t27) + 24) + 8), 48);
                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                    gm_l8 = t31;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)2ULL));
                    if (!t33) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t27) + 24) + 8));
                    gm_l7 = t34;
                    gm_b16_8 t35 = gm_l7;
                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                    gm_l8 = t36;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
                    if (!t38) {
                        goto gm_lb6;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 317, 37);
            }
gm_lb3:;
            gm_b16_8 t39 = gm_l8;
            gm_l9 = t39;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t40 = gm_l10;
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t42 = (t40 < t41);
                    if (!t42) {
                        goto gm_lb7;
                    }
                    {
                        {
                            uint64_t t43 = gm_l10;
                            void * t44 = GM_LD(void *, ((uint8_t *)&gm_l9));
                            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            uint64_t t46 = gm_l10;
                            gm_t0mglksrkfa5e t47 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t44) + (uintptr_t)t46 * 40));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l14 + 0), t43);
                            GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l14 + 8), t47);
                        }
                        {
                            {
                                uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                                gm_l11 = t48;
                                {
                                    memmove(((uint8_t *)&F.l12), (((uint8_t *)&gm_l14) + 8), 40);
                                }
                                {
                                    void * t49 = gm_l2;
                                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                    void * t51 = gm_l0;
                                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                    gm_t9qkh0rsooc8d t53;
                                    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)10ULL));
                                    gm_b16_8 t54 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t51, t52);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_th2lqbo77pvsc t55;
                                    memset(&t55, 0, sizeof t55);
                                    gm_tl5cuhjfnc4cr t56;
                                    GM_ST(uint8_t, (uint8_t *)&t56 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t56 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t56 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t50);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t56 + 16), t53);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 24), t54);
                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t56 + 40), t55);
                                    gm_tftb1odc739t5 t57 = gm_f_push_1e500nc_3(t49, &t56);
                                    gm_l15 = t57;
                                }
                                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcm0juv8ecdls4, 323, 9);
                                {
                                    {
                                        uint64_t t59 = gm_l11;
                                        uint8_t t60 = ((t59 == ((uint64_t)0ULL)));
                                        if (!t60) {
                                            goto gm_lb13;
                                        }
                                        uint32_t t61 = gm_l3;
                                        uint8_t t62 = (t61 < ((uint32_t)4ULL));
                                        if (!t62) {
                                            goto gm_lb13;
                                        }
                                        void * t63 = gm_l5;
                                        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 112));
                                        if (!t64) {
                                            goto gm_lb13;
                                        }
                                        void * t65 = gm_l0;
                                        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                        void * t67 = gm_l2;
                                        uint32_t t68 = gm_l3;
                                        uint32_t r69; bool o70 = __builtin_add_overflow(t68, ((uint32_t)1ULL), &r69);
                                        if (o70) gm_trap("integer overflow in `+`", gm_srcm0juv8ecdls4, 324, 82);
                                        uint32_t t71 = r69;
                                        gm_b0_1 t72 = gm_f_field_items_9bsf0mi((void *)NULL, cx, NULL, t65, t66, t67, t71);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 322, 5);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t73 = gm_l10;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l10 = t76;
                }
            }
gm_lb7:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_first_line_jto2r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_first_line_jto2r5e F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_src_fjcc76p(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
        gm_f_lines_of_ai9428u((void *)&F.l2, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t5 = GM_LD(void *, ((uint8_t *)&F.l2));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t7 = (((uint64_t)0ULL) < t6);
    if (t7) {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)((uint64_t)0ULL) * 16));
        GM_ST(uint8_t, (uint8_t *)&gm_l4 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l4 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l4 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l4), t8);
        ((uint8_t *)&gm_l4)[16] = 1;
    } else {
        memset(&gm_l4, 0, sizeof gm_l4);
    }
    {
        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l4), 24);
    }
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_l3 = t10;
    } else {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
        gm_l3 = t11;
    }
    gm_b16_8 t12 = gm_l3;
    gm_b16_8 t13 = gm_f_trim_end_6sjv6rg(t12);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_signature_urdu16i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2) {
    struct gm_fr_fn_signature_urdu16i F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* skip_receiver */
    void * gm_l3; /* sig */
    uint8_t gm_l5; /* skipped */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l9; /* value_param */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l11; /* text */
    void * gm_l12; /* d */
    GM_STACK_CHECK(&F, 1117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tnnjeerolos0a gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, (((uint8_t *)t19) + 344));
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 344) + 8));
    uint32_t t22 = gm_l1;
    uint64_t t23 = (uint64_t)t22;
    if ((uintptr_t)t23 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t23, t21, gm_srcm0juv8ecdls4, 60, 13);
    void * t24 = (void *)(((uint8_t *)t20) + (uintptr_t)t23 * 136);
    gm_l3 = t24;
    {
        gm_trqfvmjtt4sij t25;
        gm_f_list_epvducg((void *)&t25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t25;
    }
    uint8_t t26 = gm_l2;
    uint8_t t27 = (uint8_t)!t26;
    gm_l5 = t27;
    void * t28 = gm_l3;
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 40));
    gm_l6 = t29;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l7;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t35 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t33) + (uintptr_t)t35 * 56), 56);
                        {
                            {
                                uint8_t t36 = gm_l5;
                                uint8_t t37 = (uint8_t)!t36;
                                if (!t37) {
                                    goto gm_lb7;
                                }
                                {
                                    {
                                        uint8_t t38 = (uint8_t)(((((uint8_t *)&F.l8) + 40))[4] != 0);
                                        if (t38) {
                                            goto gm_lb9;
                                        }
                                        gm_l14 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l14 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 64, 28);
                                }
gm_lb8:;
                                uint8_t t39 = gm_l14;
                                if (!t39) {
                                    goto gm_lb7;
                                }
                                gm_l5 = ((uint8_t)1);
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        gm_l9 = ((uint8_t)0);
                        {
                            {
                                uint8_t t40 = (uint8_t)(((((uint8_t *)&F.l8) + 40))[4] != 0);
                                if (!t40) {
                                    goto gm_lb12;
                                }
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 40));
                                gm_l10 = t41;
                                void * t42 = gm_l0;
                                uint32_t t43 = gm_l10;
                                gm_tnnjeerolos0a t44 = gm_f_param_kind_a0aikmb(t42, t43);
                                gm_l16 = t44;
                                {
                                    {
                                        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                                        uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
                                        if (!t46) {
                                            goto gm_lb14;
                                        }
                                        gm_l15 = ((uint8_t)1);
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        gm_l15 = ((uint8_t)0);
                                        goto gm_lb13;
                                    }
gm_lb15:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 69, 43);
                                }
gm_lb13:;
                                uint8_t t47 = gm_l15;
                                gm_l9 = t47;
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        uint8_t t48 = gm_l9;
                        if (t48) {
                            {
                                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                void * t50 = gm_l0;
                                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                                gm_b16_8 t52 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t50, t51);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t53;
                                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t49);
                                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 16), t52);
                                gm_f_format_vfnel93((void *)&gm_l17, (void *)NULL, cx, NULL, &t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcm0juv8ecdls4, 70, 34);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                            gm_l11 = t55;
                        } else {
                            {
                                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                void * t57 = gm_l0;
                                gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                                gm_b16_8 t59 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t57, t58);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t60;
                                GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t56);
                                GM_ST(gm_b16_8, ((uint8_t *)&t60 + 16), t59);
                                gm_f_format_f6rro2t((void *)&gm_l18, (void *)NULL, cx, NULL, &t60);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                            if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcm0juv8ecdls4, 70, 89);
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                            gm_l11 = t62;
                        }
                        {
                            {
                                uint8_t t63 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 32)) != 0);
                                if (!t63) {
                                    goto gm_lb17;
                                }
                                void * t64 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                                gm_l12 = t64;
                                {
                                    gm_b16_8 t65 = gm_l11;
                                    void * t66 = gm_l0;
                                    void * t67 = gm_l12;
                                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                                    gm_b16_8 t69 = gm_f_src_fjcc76p(t66, GM_CONV(gm_cseahvg8v7p0vs, t68));
                                    gm_t6j6smj7qoctu t70;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t65);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                                    gm_f_format_279asi0((void *)&gm_l19, (void *)NULL, cx, NULL, &t70);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcm0juv8ecdls4, 71, 36);
                                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                gm_l11 = t72;
                                goto gm_lb16;
                            }
gm_lb17:;
                        }
gm_lb16:;
                        void * t73 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t74 = gm_l11;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        gm_tftb1odc739t5 t76 = gm_f_push_3ef746b(t73, t75);
                        gm_l20 = t76;
                        uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcm0juv8ecdls4, 72, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 63, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t78 = gm_l7;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            uint64_t t81 = r79;
            gm_l7 = t81;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t82;
        gm_f_list_uvfu1k1((void *)&t82, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t82;
    }
    {
        void * t83 = (void *)((uint8_t *)&F.l13);
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l21, (void *)NULL, cx, NULL, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcm0juv8ecdls4, 75, 25);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_tnpifv20irnph t89;
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
        gm_tftb1odc739t5 t90 = gm_f_write_hnivvga(t83, t89);
        gm_l22 = t90;
    }
    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcm0juv8ecdls4, 75, 5);
    void * t92 = gm_l0;
    void * t93 = (void *)(((uint8_t *)t92) + 8);
    void * t94 = gm_l3;
    gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 56));
    uint8_t t96 = gm_f_is_unit_n83ttnb(t93, GM_CONV(gm_csguankko4lmhd, t95));
    uint8_t t97 = (uint8_t)!t96;
    if (t97) {
        void * t98 = (void *)((uint8_t *)&F.l13);
        void * t99 = gm_l0;
        void * t100 = gm_l3;
        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 56));
        gm_b16_8 t102 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t99, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t103;
        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
        gm_tftb1odc739t5 t104 = gm_f_write_clopc8o(t98, t103);
        gm_l23 = t104;
        uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcm0juv8ecdls4, 76, 37);
    }
    void * t106 = gm_l3;
    uint64_t t107 = GM_LD(uint64_t, ((((uint8_t *)t106) + 64) + 8));
    uint8_t t108 = (t107 > ((uint64_t)0ULL));
    if (t108) {
        void * t109 = (void *)((uint8_t *)&F.l13);
        void * t110 = gm_l0;
        void * t111 = (void *)(((uint8_t *)t110) + 8);
        void * t112 = gm_l3;
        void * t113 = (void *)(((uint8_t *)t112) + 64);
        gm_b16_8 t114 = gm_f_show_effects_vieja9u((void *)NULL, cx, NULL, t111, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t115;
        GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
        gm_tftb1odc739t5 t116 = gm_f_write_aisrlpj(t109, t115);
        gm_l24 = t116;
        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcm0juv8ecdls4, 77, 38);
    }
    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
    gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
    F.ret = t119;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_core_file_4u6phfn(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_core_file_4u6phfn F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint8_t gm_l2;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        uint64_t t6 = (uint64_t)t5;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = GM_LD(void *, ((uint8_t *)t8));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
        uint8_t t11 = (t6 >= t10);
        gm_l2 = t11;
    }
    uint8_t t12 = gm_l2;
    if (t12) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t17 = gm_f_file_o15fqku(t15, t16);
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)t17));
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
    uint8_t t20 = gm_f_starts_with_6vvt01e(t18, t19);
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_item_text_bv1q6fm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_item_text_bv1q6fm F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* text */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_src_fjcc76p(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
    gm_l2 = t4;
    {
        gm_b16_8 t5 = gm_l2;
        gm_f_lines_of_ai9428u((void *)&F.l3, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t7 = (t6 <= ((uint64_t)24ULL));
    if (t7) {
        gm_b16_8 t8 = gm_l2;
        gm_b16_8 t9 = gm_f_trim_fdrfe6i(t8);
        F.ret = t9;
        goto gm_fret;
    }
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t10;
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l4);
        void * t12 = GM_LD(void *, ((uint8_t *)&F.l3));
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint64_t t14 = ((uintptr_t)((uint64_t)24ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l5, (void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcm0juv8ecdls4, 87, 16);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_tftb1odc739t5 t21 = gm_f_extend_0cs85gs(t11, t20);
        gm_l6 = t21;
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcm0juv8ecdls4, 87, 5);
    void * t23 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_st2a9sgu5aria));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t25 = gm_f_extend_0cs85gs(t23, t24);
    gm_l7 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcm0juv8ecdls4, 88, 5);
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    F.ret = t28;
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
__attribute__((aligned(4))) void gm_f_lines_of_ai9428u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_of_ai9428u F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* end */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t9;
    }
    gm_l2 = ((uint64_t)0ULL);
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = gm_l3;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l4;
                        gm_l5 = t14;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t17 = gm_l5;
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                        uint8_t t19 = ((t18 == ((uint8_t)10ULL)));
                        if (t19) {
                            uint64_t t20 = gm_l5;
                            gm_l6 = t20;
                            {
                                {
                                    uint64_t t21 = gm_l6;
                                    uint64_t t22 = gm_l2;
                                    uint8_t t23 = (t21 > t22);
                                    if (!t23) {
                                        goto gm_lb7;
                                    }
                                    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t26 = gm_l6;
                                    uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                                    if (o28) gm_trap("integer overflow in `-`", gm_srcm0juv8ecdls4, 43, 37);
                                    uint64_t t29 = r27;
                                    if ((uintptr_t)t29 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t29, t25, gm_srcm0juv8ecdls4, 43, 32);
                                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t29 * 1));
                                    uint8_t t31 = ((t30 == ((uint8_t)13ULL)));
                                    if (!t31) {
                                        goto gm_lb7;
                                    }
                                    uint64_t t32 = gm_l6;
                                    uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                                    if (o34) gm_trap("integer overflow in `-`", gm_srcm0juv8ecdls4, 43, 56);
                                    uint64_t t35 = r33;
                                    gm_l6 = t35;
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            void * t36 = (void *)((uint8_t *)&F.l1);
                            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t39 = gm_l2;
                            uint64_t t40 = gm_l6;
                            uint64_t t41 = (uintptr_t)t39;
                            uint64_t t42 = (uintptr_t)t40;
                            if (t41 > t42 || t42 > (uintptr_t)t38) gm_trap_slice(t41, t42, t38, gm_srcm0juv8ecdls4, 44, 22);
                            uint64_t t43 = ((uintptr_t)t40) - (uintptr_t)t39;
                            gm_b16_8 t44;
                            GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t37 + (uintptr_t)t39 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43));
                            gm_tftb1odc739t5 t45 = gm_f_push_3ef746b(t36, t44);
                            gm_l7 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcm0juv8ecdls4, 44, 13);
                            uint64_t t47 = gm_l5;
                            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                            if (o49) gm_trap("integer overflow in `+`", gm_srcm0juv8ecdls4, 45, 21);
                            uint64_t t50 = r48;
                            gm_l2 = t50;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 40, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t51 = gm_l4;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l4 = t54;
        }
    }
gm_lb1:;
    uint64_t t55 = gm_l2;
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t57 = (t55 < t56);
    if (t57) {
        void * t58 = (void *)((uint8_t *)&F.l1);
        void * t59 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t61 = gm_l2;
        uint64_t t62 = ((uintptr_t)t60) - (uintptr_t)t61;
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t59 + (uintptr_t)t61 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62));
        gm_tftb1odc739t5 t64 = gm_f_push_3ef746b(t58, t63);
        gm_l8 = t64;
        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcm0juv8ecdls4, 48, 27);
    }
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
inline __attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_module_members_26b4s91 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    uint64_t gm_l2; /* from */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 533u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        gm_trqfvmjtt4sij t12;
        gm_f_list_5dhgg4i((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t12;
    }
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 200));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 200) + 8));
    uint64_t t17 = gm_l1;
    if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srcm0juv8ecdls4, 223, 14);
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + (uintptr_t)t17 * 184) + 16));
    gm_l4 = t18;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l5;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t24 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t22) + (uintptr_t)t24 * 24), 24);
                        void * t25 = gm_l0;
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        uint64_t t27 = gm_l2;
                        uint8_t t28 = gm_f_decl_visible_bd168ti(t25, t26, t27);
                        if (t28) {
                            void * t29 = (void *)((uint8_t *)&F.l3);
                            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                            gm_tftb1odc739t5 t31 = gm_f_push_ucgmlo2(t29, t30);
                            gm_l11 = t31;
                            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcm0juv8ecdls4, 224, 43);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 223, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t33 = gm_l5;
            uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
            uint64_t t36 = r34;
            gm_l5 = t36;
        }
    }
gm_lb1:;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_f_sort_o401rjn(t37);
    {
        gm_trqfvmjtt4sij t38;
        gm_f_list_f0gjjbh_3((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t38;
    }
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_l8 = t39;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t40 = gm_l9;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t42 = (t40 < t41);
            if (!t42) {
                goto gm_lb6;
            }
            {
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t45 = gm_l9;
                {
                    {
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t43) + (uintptr_t)t45 * 8));
                        gm_l10 = t46;
                        void * t47 = (void *)((uint8_t *)&F.l7);
                        void * t48 = gm_l0;
                        uint64_t t49 = gm_l10;
                        gm_tl5cuhjfnc4cr t50;
                        gm_f_decl_item_a57s8if((void *)&t50, (void *)NULL, cx, NULL, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t51 = gm_f_push_1e500nc_3(t47, &t50);
                        gm_l12 = t51;
                        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcm0juv8ecdls4, 228, 25);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 228, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t53 = gm_l9;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l9 = t56;
        }
    }
gm_lb6:;
    {
        memmove(gm_out, ((uint8_t *)&F.l7), 40);
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
inline __attribute__((aligned(4))) void gm_f_module_scope_items_9t7jui3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_module_scope_items_9t7jui3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* d */
    GM_STACK_CHECK(&F, 500u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    {
        gm_trqfvmjtt4sij t12;
        gm_f_list_5dhgg4i((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t12;
    }
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 200));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 200) + 8));
    uint64_t t17 = gm_l1;
    if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srcm0juv8ecdls4, 261, 14);
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + (uintptr_t)t17 * 184) + 16));
    gm_l3 = t18;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l4;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t24 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t22) + (uintptr_t)t24 * 24), 24);
                        void * t25 = (void *)((uint8_t *)&F.l2);
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        gm_tftb1odc739t5 t27 = gm_f_push_ucgmlo2(t25, t26);
                        gm_l10 = t27;
                        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcm0juv8ecdls4, 261, 49);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 261, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l4;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l4 = t32;
        }
    }
gm_lb1:;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_f_sort_o401rjn(t33);
    {
        gm_trqfvmjtt4sij t34;
        gm_f_list_f0gjjbh_3((void *)&t34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t34;
    }
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l7 = t35;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t36 = gm_l8;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t38 = (t36 < t37);
            if (!t38) {
                goto gm_lb6;
            }
            {
                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t41 = gm_l8;
                {
                    {
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t39) + (uintptr_t)t41 * 8));
                        gm_l9 = t42;
                        void * t43 = (void *)((uint8_t *)&F.l6);
                        void * t44 = gm_l0;
                        uint64_t t45 = gm_l9;
                        gm_tl5cuhjfnc4cr t46;
                        gm_f_decl_item_a57s8if((void *)&t46, (void *)NULL, cx, NULL, t44, t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t47 = gm_f_push_1e500nc_3(t43, &t46);
                        gm_l11 = t47;
                        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcm0juv8ecdls4, 264, 25);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 264, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t49 = gm_l8;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l8 = t52;
        }
    }
gm_lb6:;
    {
        memmove(gm_out, ((uint8_t *)&F.l6), 40);
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
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_op_info_4a09t36 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* ed */
    uint32_t gm_l2; /* index */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* name */
    void * gm_l6; /* lit */
    gm_th2lqbo77pvsc gm_l7; /* doc */
    gm_b16_8 gm_l8; /* code */
    uint32_t gm_l10; /* e */
    void * gm_l11; /* info */
    void * gm_l12; /* op */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* ret */
    gm_b16_8 gm_l18; /* kw */
    GM_STACK_CHECK(&F, 1562u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tlabdfckddbfd gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 240));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 240) + 8));
    uint64_t t23 = gm_l1;
    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcm0juv8ecdls4, 101, 14);
    void * t24 = (void *)(((uint8_t *)t21) + (uintptr_t)t23 * 216);
    gm_l3 = t24;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(0));
    gm_l4 = t25;
    {
        {
            void * t26 = gm_l3;
            uint8_t t27 = GM_LD(uint8_t, (((((uint8_t *)t26) + 40) + 64) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
            if (!t28) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), (((((uint8_t *)t26) + 40) + 64) + 8), 64);
            {
                {
                    void * t29 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)34ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    void * t32 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
                    gm_l6 = t32;
                    void * t33 = gm_l6;
                    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 24));
                    uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 24) + 8));
                    uint32_t t36 = gm_l2;
                    uint64_t t37 = (uint64_t)t36;
                    if ((uintptr_t)t37 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t37, t35, gm_srcm0juv8ecdls4, 104, 49);
                    gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t37 * 80));
                    gm_l4 = t38;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t39 = gm_l3;
        memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t39) + 40) + 16), 24);
    }
    gm_b16_8 t40 = gm_l4;
    gm_l8 = t40;
    {
        {
            {
                void * t41 = gm_l0;
                uint64_t t42 = gm_l1;
                gm_f_decl_done_mk70ol9((void *)&gm_l19, (void *)NULL, cx, NULL, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l19))[208] != 0);
            if (!t43) {
                goto gm_lb6;
            }
            {
                memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l19), 208);
            }
            {
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)2ULL));
                    if (!t45) {
                        goto gm_lb8;
                    }
                    uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
                    gm_l10 = t46;
                    void * t47 = gm_l0;
                    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 1048));
                    uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 1048) + 8));
                    uint32_t t50 = gm_l10;
                    uint64_t t51 = (uint64_t)t50;
                    if ((uintptr_t)t51 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t51, t49, gm_srcm0juv8ecdls4, 110, 22);
                    void * t52 = (void *)(((uint8_t *)t48) + (uintptr_t)t51 * 56);
                    gm_l11 = t52;
                    uint32_t t53 = gm_l2;
                    uint64_t t54 = (uint64_t)t53;
                    void * t55 = gm_l11;
                    uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)t55) + 40) + 8));
                    uint8_t t57 = (t54 < t56);
                    if (t57) {
                        void * t58 = gm_l11;
                        void * t59 = GM_LD(void *, (((uint8_t *)t58) + 40));
                        uint64_t t60 = GM_LD(uint64_t, ((((uint8_t *)t58) + 40) + 8));
                        uint32_t t61 = gm_l2;
                        uint64_t t62 = (uint64_t)t61;
                        if ((uintptr_t)t62 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t62, t60, gm_srcm0juv8ecdls4, 112, 24);
                        void * t63 = (void *)(((uint8_t *)t59) + (uintptr_t)t62 * 48);
                        gm_l12 = t63;
                        {
                            gm_trqfvmjtt4sij t64;
                            gm_f_list_epvducg((void *)&t64, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l13 = t64;
                        }
                        void * t65 = gm_l12;
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t65) + 24));
                        gm_l14 = t66;
                        gm_l15 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t67 = gm_l15;
                                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint8_t t69 = (t67 < t68);
                                if (!t69) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint64_t t72 = gm_l15;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l16), (((uint8_t *)t70) + (uintptr_t)t72 * 56), 56);
                                            }
                                            {
                                                void * t73 = (void *)((uint8_t *)&F.l13);
                                                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                                void * t75 = gm_l0;
                                                gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                                                gm_b16_8 t77 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t75, t76);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t6j6smj7qoctu t78;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t74);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 16), t77);
                                                gm_f_format_f6rro2t((void *)&gm_l20, (void *)NULL, cx, NULL, &t78);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                                if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcm0juv8ecdls4, 114, 46);
                                                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                                                gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t73, t81);
                                                gm_l21 = t82;
                                            }
                                            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                            if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcm0juv8ecdls4, 114, 38);
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 114, 17);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t84 = gm_l15;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                uint64_t t87 = r85;
                                gm_l15 = t87;
                            }
                        }
gm_lb9:;
                        void * t88 = gm_l0;
                        void * t89 = (void *)(((uint8_t *)t88) + 8);
                        void * t90 = gm_l12;
                        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 40));
                        uint8_t t92 = gm_f_is_unit_n83ttnb(t89, GM_CONV(gm_csguankko4lmhd, t91));
                        if (t92) {
                            gm_b16_8 t93;
                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(0));
                            gm_l17 = t93;
                        } else {
                            void * t94 = gm_l0;
                            void * t95 = gm_l12;
                            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t95) + 40));
                            gm_b16_8 t97 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t94, t96);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t98;
                            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
                            gm_f_format_0555s29((void *)&gm_l22, (void *)NULL, cx, NULL, t98);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                            if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcm0juv8ecdls4, 115, 63);
                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                            gm_l17 = t101;
                        }
                        void * t102 = gm_l12;
                        {
                            {
                                uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t102) + 16) + 0));
                                uint8_t t104 = (uint8_t)(t103 == ((uint8_t)0ULL));
                                if (!t104) {
                                    goto gm_lb15;
                                }
                                gm_b16_8 t105;
                                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s579fb7l5l6df));
                                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(2));
                                gm_l18 = t105;
                                goto gm_lb14;
                            }
gm_lb15:;
                            {
                                uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t102) + 16) + 0));
                                uint8_t t107 = (uint8_t)(t106 == ((uint8_t)1ULL));
                                if (!t107) {
                                    goto gm_lb16;
                                }
                                gm_b16_8 t108;
                                GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sb88rajuutaep));
                                GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(3));
                                gm_l18 = t108;
                                goto gm_lb14;
                            }
gm_lb16:;
                            {
                                uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t102) + 16) + 0));
                                uint8_t t110 = (uint8_t)(t109 == ((uint8_t)2ULL));
                                if (!t110) {
                                    goto gm_lb17;
                                }
                                gm_b16_8 t111;
                                GM_ST(void *, (uint8_t *)&t111, (void *)(gm_s66uvortgrtsg));
                                GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(3));
                                gm_l18 = t111;
                                goto gm_lb14;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 116, 23);
                        }
gm_lb14:;
                        {
                            void * t112 = gm_l11;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)t112));
                            void * t114 = gm_l12;
                            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)t114));
                            gm_b16_8 t116 = gm_l18;
                            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                            gm_b16_8 t119;
                            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(2));
                            gm_f_join_sfknljb((void *)&gm_l23, (void *)NULL, cx, NULL, t118, t119);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcm0juv8ecdls4, 121, 80);
                            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                            gm_b16_8 t122 = gm_l17;
                            gm_tud7oovfvnf43 t123;
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t113);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 16), t115);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 32), t116);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 48), t121);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 64), t122);
                            gm_f_format_9ndqnou((void *)&gm_l24, (void *)NULL, cx, NULL, &t123);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcm0juv8ecdls4, 121, 24);
                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                        gm_l8 = t126;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
            }
gm_lb7:;
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    {
        gm_b16_8 t127 = gm_l8;
        gm_th2lqbo77pvsc t128 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l7));
        gm_t6ok5tvcjc200 t129;
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t127);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t129 + 16), t128);
        gm_t9qkh0rsooc8d t130;
        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)12ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 0), t130);
        GM_ST(gm_t6ok5tvcjc200, ((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 8), t129);
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
inline __attribute__((aligned(4))) void gm_f_op_item_8dfr8db(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_op_item_8dfr8db F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* ed */
    uint32_t gm_l2; /* index */
    gm_b16_8 gm_l4; /* detail */
    gm_b16_8 gm_l5; /* name */
    void * gm_l7; /* lit */
    GM_STACK_CHECK(&F, 467u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_trqfvmjtt4sij gm_l8;
    gm_b16_8 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    {
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        uint32_t t8 = gm_l2;
        gm_f_op_info_4a09t36((void *)&F.l3, (void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
        gm_f_lines_of_ai9428u((void *)&gm_l8, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
    gm_l9 = t10;
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l9));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint8_t t13 = (((uint64_t)0ULL) < t12);
    if (t13) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)((uint64_t)0ULL) * 16));
        GM_ST(uint8_t, (uint8_t *)&gm_l10 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l10 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l10 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), t14);
        ((uint8_t *)&gm_l10)[16] = 1;
    } else {
        memset(&gm_l10, 0, sizeof gm_l10);
    }
    {
        memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l10), 24);
    }
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
    if (t15) {
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l4 = t16;
    } else {
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
        gm_l4 = t17;
    }
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(0));
    gm_l5 = t18;
    {
        {
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, ((uint8_t *)t19));
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 240));
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 240) + 8));
            uint64_t t23 = gm_l1;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcm0juv8ecdls4, 214, 23);
            uint8_t t24 = GM_LD(uint8_t, ((((((uint8_t *)t21) + (uintptr_t)t23 * 216) + 40) + 64) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)0ULL));
            if (!t25) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l6), ((((((uint8_t *)t21) + (uintptr_t)t23 * 216) + 40) + 64) + 8), 64);
            {
                {
                    void * t26 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)34ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    void * t29 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
                    gm_l7 = t29;
                    void * t30 = gm_l7;
                    void * t31 = GM_LD(void *, (((uint8_t *)t30) + 24));
                    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t30) + 24) + 8));
                    uint32_t t33 = gm_l2;
                    uint64_t t34 = (uint64_t)t33;
                    if ((uintptr_t)t34 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t34, t32, gm_srcm0juv8ecdls4, 215, 49);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t31) + (uintptr_t)t34 * 80));
                    gm_l5 = t35;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_b16_8 t36 = gm_l5;
        gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l3));
        gm_b16_8 t38 = gm_l4;
        gm_th2lqbo77pvsc t39 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)&F.l3) + 8) + 16));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 0), t36);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 16), t37);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 24), t38);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 40), t39);
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
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_operation_members_62l824c F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l4; /* e */
    void * gm_l5; /* info */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* ret */
    GM_STACK_CHECK(&F, 1205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlabdfckddbfd gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    {
        gm_trqfvmjtt4sij t14;
        gm_f_list_f0gjjbh_3((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t14;
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = gm_l1;
        gm_f_decl_done_mk70ol9((void *)&gm_l14, (void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l14))[208] != 0);
    if (t17) {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l14), 208);
    } else {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
        goto gm_fdone;
    }
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
            if (!t19) {
                goto gm_lb2;
            }
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            gm_l4 = t20;
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1048));
            uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1048) + 8));
            uint32_t t24 = gm_l4;
            uint64_t t25 = (uint64_t)t24;
            if ((uintptr_t)t25 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t25, t23, gm_srcm0juv8ecdls4, 346, 14);
            void * t26 = (void *)(((uint8_t *)t22) + (uintptr_t)t25 * 56);
            gm_l5 = t26;
            void * t27 = gm_l5;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 40));
            gm_l6 = t28;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t29 = gm_l7;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t31 = (t29 < t30);
                    if (!t31) {
                        goto gm_lb3;
                    }
                    {
                        void * t32 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t34 = gm_l7;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l8), (((uint8_t *)t32) + (uintptr_t)t34 * 48), 48);
                                }
                                {
                                    gm_trqfvmjtt4sij t35;
                                    gm_f_list_epvducg((void *)&t35, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l9 = t35;
                                }
                                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                                gm_l10 = t36;
                                gm_l11 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t37 = gm_l11;
                                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint8_t t39 = (t37 < t38);
                                        if (!t39) {
                                            goto gm_lb8;
                                        }
                                        {
                                            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint64_t t42 = gm_l11;
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l12), (((uint8_t *)t40) + (uintptr_t)t42 * 56), 56);
                                                    }
                                                    {
                                                        void * t43 = (void *)((uint8_t *)&F.l9);
                                                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                        void * t45 = gm_l0;
                                                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                                        gm_b16_8 t47 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t45, t46);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_t6j6smj7qoctu t48;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t44);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 16), t47);
                                                        gm_f_format_f6rro2t((void *)&gm_l15, (void *)NULL, cx, NULL, &t48);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcm0juv8ecdls4, 349, 38);
                                                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                                        gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t43, t51);
                                                        gm_l16 = t52;
                                                    }
                                                    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                                    if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcm0juv8ecdls4, 349, 30);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 349, 9);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t54 = gm_l11;
                                        uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                        uint64_t t57 = r55;
                                        gm_l11 = t57;
                                    }
                                }
gm_lb8:;
                                void * t58 = gm_l0;
                                void * t59 = (void *)(((uint8_t *)t58) + 8);
                                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 40));
                                uint8_t t61 = gm_f_is_unit_n83ttnb(t59, GM_CONV(gm_csguankko4lmhd, t60));
                                if (t61) {
                                    gm_b16_8 t62;
                                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(0));
                                    gm_l13 = t62;
                                } else {
                                    void * t63 = gm_l0;
                                    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 40));
                                    gm_b16_8 t65 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t63, t64);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t66;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                                    gm_f_format_0555s29((void *)&gm_l17, (void *)NULL, cx, NULL, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcm0juv8ecdls4, 350, 55);
                                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                                    gm_l13 = t69;
                                }
                                {
                                    void * t70 = (void *)((uint8_t *)&F.l2);
                                    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                    gm_b16_8 t74;
                                    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sg5gdnrith2i6));
                                    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
                                    gm_t9qkh0rsooc8d t75;
                                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)12ULL));
                                    gm_f_join_sfknljb((void *)&gm_l18, (void *)NULL, cx, NULL, t73, t74);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                    if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcm0juv8ecdls4, 351, 88);
                                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                    gm_b16_8 t78 = gm_l13;
                                    gm_t6j6smj7qoctu t79;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t77);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 16), t78);
                                    gm_f_format_22ud8a7((void *)&gm_l19, (void *)NULL, cx, NULL, &t79);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                    if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcm0juv8ecdls4, 351, 67);
                                    gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                    gm_th2lqbo77pvsc t83;
                                    memset(&t83, 0, sizeof t83);
                                    gm_tl5cuhjfnc4cr t84;
                                    GM_ST(uint8_t, (uint8_t *)&t84 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t84 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t84 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t71);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t84 + 16), t75);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 24), t82);
                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t84 + 40), t83);
                                    gm_tftb1odc739t5 t85 = gm_f_push_1e500nc_3(t70, &t84);
                                    gm_l20 = t85;
                                }
                                uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcm0juv8ecdls4, 351, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 347, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t87 = gm_l7;
                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                    uint64_t t90 = r88;
                    gm_l7 = t90;
                }
            }
gm_lb3:;
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
gm_lb2:;
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_src_fjcc76p F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint8_t gm_l2;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        uint64_t t6 = (uint64_t)t5;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = GM_LD(void *, ((uint8_t *)t8));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
        uint8_t t11 = (t6 >= t10);
        gm_l2 = t11;
    }
    uint8_t t12 = gm_l2;
    if (t12) {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
        F.ret = t13;
        goto gm_fret;
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t18 = gm_f_file_o15fqku(t16, t17);
    void * t19 = GM_LD(void *, (((uint8_t *)t18) + 16));
    uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 16) + 8));
    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t22 = (uint64_t)t21;
    uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t24 = (uint64_t)t23;
    uint64_t t25 = (uintptr_t)t22;
    uint64_t t26 = (uintptr_t)t24;
    if (t25 > t26 || t26 > (uintptr_t)t20) gm_trap_slice(t25, t26, t20, gm_srcm0juv8ecdls4, 27, 12);
    uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)t22;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t19 + (uintptr_t)t22 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_universe_items_235nbu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_universe_items_235nbu8 F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    void * gm_l7; /* decl */
    gm_tf5srh4mp4b3i gm_l8; /* yield_decl */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* d */
    uint64_t gm_l12; /* y */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* n */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* n */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* k */
    GM_STACK_CHECK(&F, 1688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_f0gjjbh_3((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t33;
    }
    {
        {
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, ((uint8_t *)t34));
            uint8_t t36 = (uint8_t)(((((uint8_t *)t35) + 320))[8] != 0);
            if (!t36) {
                goto gm_lb2;
            }
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t35) + 320));
            gm_l2 = t37;
            gm_trqfvmjtt4sij t38;
            gm_f_list_5dhgg4i((void *)&t38, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t38;
            void * t39 = gm_l0;
            void * t40 = GM_LD(void *, ((uint8_t *)t39));
            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 200));
            uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 200) + 8));
            uint64_t t43 = gm_l2;
            if ((uintptr_t)t43 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t43, t42, gm_srcm0juv8ecdls4, 237, 18);
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t41) + (uintptr_t)t43 * 184) + 16));
            gm_l4 = t44;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t45 = gm_l5;
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t47 = (t45 < t46);
                    if (!t47) {
                        goto gm_lb3;
                    }
                    {
                        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t50 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)t48) + (uintptr_t)t50 * 24), 24);
                                void * t51 = gm_l0;
                                void * t52 = GM_LD(void *, ((uint8_t *)t51));
                                void * t53 = GM_LD(void *, (((uint8_t *)t52) + 240));
                                uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t52) + 240) + 8));
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                                if ((uintptr_t)t55 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t55, t54, gm_srcm0juv8ecdls4, 238, 22);
                                void * t56 = (void *)(((uint8_t *)t53) + (uintptr_t)t55 * 216);
                                gm_l7 = t56;
                                {
                                    {
                                        void * t57 = gm_l7;
                                        gm_t9qkh0rsooc8d t58 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t57) + 40) + 56));
                                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                                        uint8_t t60 = (uint8_t)((t59 == ((uint8_t)2ULL)));
                                        if (!t60) {
                                            goto gm_lb9;
                                        }
                                        void * t61 = gm_l0;
                                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                                        gm_t9qkh0rsooc8d t63;
                                        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)2ULL));
                                        uint8_t t64 = gm_f_universe_hidden_gsk2b8r(t61, t62);
                                        uint8_t t65 = (uint8_t)!t64;
                                        if (!t65) {
                                            goto gm_lb9;
                                        }
                                        void * t66 = gm_l7;
                                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)t66));
                                        gm_b16_8 t68;
                                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_st3p5nb1e6cc8));
                                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
                                        uint8_t t69 = (!gm_eq0iqiubfm7e(((uint8_t *)&t67), ((uint8_t *)&t68)));
                                        if (!t69) {
                                            goto gm_lb9;
                                        }
                                        void * t70 = (void *)((uint8_t *)&F.l3);
                                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                                        gm_tftb1odc739t5 t72 = gm_f_push_ucgmlo2(t70, t71);
                                        gm_l22 = t72;
                                        uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                        if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcm0juv8ecdls4, 239, 116);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 237, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t74 = gm_l5;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l5 = t77;
                }
            }
gm_lb3:;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_f_sort_o401rjn(t78);
            void * t79 = gm_l0;
            void * t80 = GM_LD(void *, ((uint8_t *)t79));
            uint64_t t81 = gm_l2;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_ses7tdrdt8a6u));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(5));
            gm_tf5srh4mp4b3i t83 = gm_f_lookup_59juqoo(t80, t81, t82);
            gm_l8 = t83;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_l9 = t84;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t85 = gm_l10;
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t87 = (t85 < t86);
                    if (!t87) {
                        goto gm_lb10;
                    }
                    {
                        void * t88 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t90 = gm_l10;
                        {
                            {
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t88) + (uintptr_t)t90 * 8));
                                gm_l11 = t91;
                                void * t92 = (void *)((uint8_t *)&F.l1);
                                void * t93 = gm_l0;
                                uint64_t t94 = gm_l11;
                                gm_tl5cuhjfnc4cr t95;
                                gm_f_decl_item_a57s8if((void *)&t95, (void *)NULL, cx, NULL, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t96 = gm_f_push_1e500nc_3(t92, &t95);
                                gm_l23 = t96;
                                uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcm0juv8ecdls4, 244, 13);
                                {
                                    {
                                        uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                        if (!t98) {
                                            goto gm_lb16;
                                        }
                                        uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                                        gm_l12 = t99;
                                        uint64_t t100 = gm_l12;
                                        uint64_t t101 = gm_l11;
                                        uint8_t t102 = ((t100 == t101));
                                        if (t102) {
                                            void * t103 = (void *)((uint8_t *)&F.l1);
                                            void * t104 = gm_l0;
                                            uint64_t t105 = gm_l12;
                                            gm_tl5cuhjfnc4cr t106;
                                            gm_f_op_item_8dfr8db((void *)&t106, (void *)NULL, cx, NULL, t104, t105, ((uint32_t)0ULL));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t107 = gm_f_push_1e500nc_3(t103, &t106);
                                            gm_l24 = t107;
                                            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcm0juv8ecdls4, 248, 29);
                                        }
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 243, 9);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t109 = gm_l10;
                    uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                    uint64_t t112 = r110;
                    gm_l10 = t112;
                }
            }
gm_lb10:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t113 = (void *)((uint8_t *)gm_c4tbnbi68ao);
    uint64_t t114 = ((uintptr_t)((uint64_t)20ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t115;
    GM_ST(void *, (uint8_t *)&t115, (void *)(((uint8_t *)t113 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(t114));
    gm_l13 = t115;
    gm_l14 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t116 = gm_l14;
            uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            uint8_t t118 = (t116 < t117);
            if (!t118) {
                goto gm_lb17;
            }
            {
                void * t119 = GM_LD(void *, ((uint8_t *)&gm_l13));
                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t t121 = gm_l14;
                {
                    {
                        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)t119) + (uintptr_t)t121 * 16));
                        gm_l15 = t122;
                        {
                            void * t123 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t124 = gm_l15;
                            gm_b16_8 t125;
                            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sk8fkllqjst4j));
                            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(12));
                            gm_th2lqbo77pvsc t126;
                            memset(&t126, 0, sizeof t126);
                            gm_t9qkh0rsooc8d t127;
                            GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)14ULL));
                            gm_tl5cuhjfnc4cr t128;
                            GM_ST(uint8_t, (uint8_t *)&t128 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t128 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t128 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t124);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t128 + 16), t127);
                            GM_ST(gm_b16_8, ((uint8_t *)&t128 + 24), t125);
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t128 + 40), t126);
                            gm_tftb1odc739t5 t129 = gm_f_push_1e500nc_3(t123, &t128);
                            gm_l25 = t129;
                        }
                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcm0juv8ecdls4, 252, 27);
                        goto gm_lb20;
                    }
gm_lb21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 252, 5);
                }
gm_lb20:;
            }
gm_lb19:;
            uint64_t t131 = gm_l14;
            uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
            uint64_t t134 = r132;
            gm_l14 = t134;
        }
    }
gm_lb17:;
    void * t135 = (void *)((uint8_t *)gm_cjfp4khah06);
    uint64_t t136 = ((uintptr_t)((uint64_t)5ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t137;
    GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t135 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(t136));
    gm_l16 = t137;
    gm_l17 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t138 = gm_l17;
            uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
            uint8_t t140 = (t138 < t139);
            if (!t140) {
                goto gm_lb22;
            }
            {
                void * t141 = GM_LD(void *, ((uint8_t *)&gm_l16));
                uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint64_t t143 = gm_l17;
                {
                    {
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t141) + (uintptr_t)t143 * 16));
                        gm_l18 = t144;
                        {
                            void * t145 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t146 = gm_l18;
                            gm_b16_8 t147;
                            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s3s4n6ehcrs2b));
                            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(16));
                            gm_th2lqbo77pvsc t148;
                            memset(&t148, 0, sizeof t148);
                            gm_t9qkh0rsooc8d t149;
                            GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)1ULL));
                            gm_tl5cuhjfnc4cr t150;
                            GM_ST(uint8_t, (uint8_t *)&t150 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t150 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t150 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t146);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t150 + 16), t149);
                            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 24), t147);
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t150 + 40), t148);
                            gm_tftb1odc739t5 t151 = gm_f_push_1e500nc_3(t145, &t150);
                            gm_l26 = t151;
                        }
                        uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcm0juv8ecdls4, 253, 28);
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 253, 5);
                }
gm_lb25:;
            }
gm_lb24:;
            uint64_t t153 = gm_l17;
            uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
            uint64_t t156 = r154;
            gm_l17 = t156;
        }
    }
gm_lb22:;
    void * t157 = (void *)((uint8_t *)gm_cka4oohkshv);
    uint64_t t158 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t159;
    GM_ST(void *, (uint8_t *)&t159, (void *)(((uint8_t *)t157 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(t158));
    gm_l19 = t159;
    gm_l20 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t160 = gm_l20;
            uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
            uint8_t t162 = (t160 < t161);
            if (!t162) {
                goto gm_lb27;
            }
            {
                void * t163 = GM_LD(void *, ((uint8_t *)&gm_l19));
                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint64_t t165 = gm_l20;
                {
                    {
                        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)t163) + (uintptr_t)t165 * 16));
                        gm_l21 = t166;
                        {
                            void * t167 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t168 = gm_l21;
                            gm_b16_8 t169;
                            GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(0));
                            gm_th2lqbo77pvsc t170;
                            memset(&t170, 0, sizeof t170);
                            gm_t9qkh0rsooc8d t171;
                            GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)16ULL));
                            gm_tl5cuhjfnc4cr t172;
                            GM_ST(uint8_t, (uint8_t *)&t172 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t172 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t172 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t168);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t172 + 16), t171);
                            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 24), t169);
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t172 + 40), t170);
                            gm_tftb1odc739t5 t173 = gm_f_push_1e500nc_3(t167, &t172);
                            gm_l27 = t173;
                        }
                        uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcm0juv8ecdls4, 254, 37);
                        goto gm_lb30;
                    }
gm_lb31:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 254, 5);
                }
gm_lb30:;
            }
gm_lb29:;
            uint64_t t175 = gm_l20;
            uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
            uint64_t t178 = r176;
            gm_l20 = t178;
        }
    }
gm_lb27:;
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
__attribute__((aligned(4))) void gm_f_value_members_03rtuq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_value_members_03rtuq5 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    uint32_t gm_l5; /* template */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t3877pk7lv748 gm_l9; /* n */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* d */
    uint32_t gm_l14; /* f */
    void * gm_l15; /* sig */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l21; /* recv */
    gm_t88rcucg1b7uk gm_l22; /* pointee */
    uint8_t gm_l23; /* fits */
    uint32_t gm_l24; /* x */
    void * gm_l25; /* decl */
    GM_STACK_CHECK(&F, 1582u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tlabdfckddbfd gm_l28;
    uint8_t gm_l29;
    gm_t53at80pfdus3 gm_l30;
    gm_t53at80pfdus3 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        gm_trqfvmjtt4sij t39;
        gm_f_list_f0gjjbh_3((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t39;
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t88rcucg1b7uk t42 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l26, t41, GM_CONV(gm_csguankko4lmhd, t42));
        {
            {
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)17ULL));
                    if (!t44) {
                        goto gm_lb2;
                    }
                    uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 8));
                    gm_l3 = t45;
                    void * t46 = gm_l0;
                    gm_t88rcucg1b7uk t47 = gm_l1;
                    void * t48 = (void *)((uint8_t *)&F.l2);
                    gm_b0_1 t49 = gm_f_field_items_9bsf0mi((void *)NULL, cx, NULL, t46, t47, t48, ((uint32_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t50 = gm_l0;
                    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 1088));
                    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 1088) + 8));
                    uint32_t t53 = gm_l3;
                    uint64_t t54 = (uint64_t)t53;
                    if ((uintptr_t)t54 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t54, t52, gm_srcm0juv8ecdls4, 279, 22);
                    void * t55 = (void *)(((uint8_t *)t51) + (uintptr_t)t54 * 184);
                    gm_l4 = t55;
                    void * t56 = gm_l0;
                    void * t57 = (void *)(((uint8_t *)t56) + 8);
                    uint32_t t58 = gm_l3;
                    uint32_t t59 = gm_f_template_9o92cvv(t57, t58);
                    gm_l5 = t59;
                    gm_trqfvmjtt4sij t60;
                    gm_f_list_5dhgg4i((void *)&t60, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l6 = t60;
                    void * t61 = gm_l0;
                    void * t62 = GM_LD(void *, ((uint8_t *)t61));
                    void * t63 = GM_LD(void *, (((uint8_t *)t62) + 200));
                    uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t62) + 200) + 8));
                    void * t65 = gm_l4;
                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 16));
                    if ((uintptr_t)t66 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t66, t64, gm_srcm0juv8ecdls4, 282, 22);
                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)t63) + (uintptr_t)t66 * 184) + 16));
                    gm_l7 = t67;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t68 = gm_l8;
                            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t70 = (t68 < t69);
                            if (!t70) {
                                goto gm_lb3;
                            }
                            {
                                void * t71 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t73 = gm_l8;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l9), (((uint8_t *)t71) + (uintptr_t)t73 * 24), 24);
                                        void * t74 = (void *)((uint8_t *)&F.l6);
                                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 16));
                                        gm_tftb1odc739t5 t76 = gm_f_push_ucgmlo2(t74, t75);
                                        gm_l27 = t76;
                                        uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                        if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcm0juv8ecdls4, 282, 67);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 282, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t78 = gm_l8;
                            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                            uint64_t t81 = r79;
                            gm_l8 = t81;
                        }
                    }
gm_lb3:;
                    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_f_sort_o401rjn(t82);
                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_l10 = t83;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t84 = gm_l11;
                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t86 = (t84 < t85);
                            if (!t86) {
                                goto gm_lb8;
                            }
                            {
                                void * t87 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t89 = gm_l11;
                                {
                                    {
                                        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)t87) + (uintptr_t)t89 * 8));
                                        gm_l12 = t90;
                                        void * t91 = gm_l0;
                                        uint64_t t92 = gm_l12;
                                        gm_f_decl_done_mk70ol9((void *)&gm_l28, (void *)NULL, cx, NULL, t91, t92);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t93 = (uint8_t)((((uint8_t *)&gm_l28))[208] != 0);
                                        if (t93) {
                                            memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l28), 208);
                                        } else {
                                            goto gm_lb10;
                                        }
                                        {
                                            {
                                                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                                uint8_t t95 = (uint8_t)(t94 == ((uint8_t)5ULL));
                                                if (!t95) {
                                                    goto gm_lb14;
                                                }
                                                uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)&F.l13) + 8));
                                                gm_l14 = t96;
                                                void * t97 = gm_l0;
                                                void * t98 = GM_LD(void *, (((uint8_t *)t97) + 344));
                                                uint64_t t99 = GM_LD(uint64_t, ((((uint8_t *)t97) + 344) + 8));
                                                uint32_t t100 = gm_l14;
                                                uint64_t t101 = (uint64_t)t100;
                                                if ((uintptr_t)t101 >= (uintptr_t)(t99)) gm_trap_index((uintptr_t)t101, t99, gm_srcm0juv8ecdls4, 287, 25);
                                                void * t102 = (void *)(((uint8_t *)t98) + (uintptr_t)t101 * 136);
                                                gm_l15 = t102;
                                                memset(&F.l16, 0, sizeof F.l16);
                                                void * t103 = gm_l15;
                                                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t103) + 40));
                                                gm_l17 = t104;
                                                gm_l18 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t105 = gm_l18;
                                                        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                                        uint8_t t107 = (t105 < t106);
                                                        if (!t107) {
                                                            goto gm_lb15;
                                                        }
                                                        {
                                                            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                                            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                                            uint64_t t110 = gm_l18;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&F.l19), (((uint8_t *)t108) + (uintptr_t)t110 * 56), 56);
                                                                    {
                                                                        {
                                                                            uint8_t t111 = (uint8_t)(((((uint8_t *)&F.l19) + 40))[4] != 0);
                                                                            if (t111) {
                                                                                goto gm_lb21;
                                                                            }
                                                                            gm_l29 = ((uint8_t)1);
                                                                            goto gm_lb20;
                                                                        }
gm_lb21:;
                                                                        {
                                                                            gm_l29 = ((uint8_t)0);
                                                                            goto gm_lb20;
                                                                        }
gm_lb22:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 290, 24);
                                                                    }
gm_lb20:;
                                                                    uint8_t t112 = gm_l29;
                                                                    if (t112) {
                                                                        {
                                                                            gm_t5v0c8ia6a5mo t113 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)&F.l19));
                                                                            GM_ST(uint8_t, (uint8_t *)&F.l16 + 57, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&F.l16 + 58, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&F.l16 + 60, 0);
                                                                            GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&F.l16), t113);
                                                                            ((uint8_t *)&F.l16)[56] = 1;
                                                                        }
                                                                        goto gm_lb15;
                                                                    }
                                                                    goto gm_lb18;
                                                                }
gm_lb19:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 289, 17);
                                                            }
gm_lb18:;
                                                        }
gm_lb17:;
                                                        uint64_t t114 = gm_l18;
                                                        uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                                                        uint64_t t117 = r115;
                                                        gm_l18 = t117;
                                                    }
                                                }
gm_lb15:;
                                                uint8_t t118 = (uint8_t)((((uint8_t *)&F.l16))[56] != 0);
                                                if (t118) {
                                                    memmove(((uint8_t *)&F.l20), ((uint8_t *)&F.l16), 56);
                                                } else {
                                                    goto gm_lb10;
                                                }
                                                gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l20) + 16));
                                                gm_l21 = t119;
                                                {
                                                    {
                                                        void * t120 = gm_l0;
                                                        void * t121 = (void *)(((uint8_t *)t120) + 8);
                                                        gm_t88rcucg1b7uk t122 = gm_l21;
                                                        gm_f_kind_jcnt4hm((void *)&gm_l30, t121, GM_CONV(gm_csguankko4lmhd, t122));
                                                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                                                        uint8_t t124 = (uint8_t)(t123 == ((uint8_t)8ULL));
                                                        if (!t124) {
                                                            goto gm_lb24;
                                                        }
                                                        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l30) + 8) + 4));
                                                        gm_l22 = t125;
                                                        gm_t88rcucg1b7uk t126 = gm_l22;
                                                        gm_l21 = t126;
                                                        goto gm_lb23;
                                                    }
gm_lb24:;
                                                }
gm_lb23:;
                                                gm_l23 = ((uint8_t)0);
                                                {
                                                    {
                                                        void * t127 = gm_l0;
                                                        void * t128 = (void *)(((uint8_t *)t127) + 8);
                                                        gm_t88rcucg1b7uk t129 = gm_l21;
                                                        gm_f_kind_jcnt4hm((void *)&gm_l31, t128, GM_CONV(gm_csguankko4lmhd, t129));
                                                        uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                                                        uint8_t t131 = (uint8_t)(t130 == ((uint8_t)17ULL));
                                                        if (!t131) {
                                                            goto gm_lb26;
                                                        }
                                                        uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 8));
                                                        gm_l24 = t132;
                                                        void * t133 = gm_l0;
                                                        void * t134 = (void *)(((uint8_t *)t133) + 8);
                                                        uint32_t t135 = gm_l24;
                                                        uint32_t t136 = gm_f_template_9o92cvv(t134, t135);
                                                        uint32_t t137 = gm_l5;
                                                        uint8_t t138 = ((t136 == t137));
                                                        gm_l23 = t138;
                                                        goto gm_lb25;
                                                    }
gm_lb26:;
                                                }
gm_lb25:;
                                                uint8_t t139 = gm_l23;
                                                if (t139) {
                                                    void * t140 = gm_l0;
                                                    void * t141 = GM_LD(void *, ((uint8_t *)t140));
                                                    void * t142 = GM_LD(void *, (((uint8_t *)t141) + 240));
                                                    uint64_t t143 = GM_LD(uint64_t, ((((uint8_t *)t141) + 240) + 8));
                                                    uint64_t t144 = gm_l12;
                                                    if ((uintptr_t)t144 >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)t144, t143, gm_srcm0juv8ecdls4, 301, 30);
                                                    void * t145 = (void *)(((uint8_t *)t142) + (uintptr_t)t144 * 216);
                                                    gm_l25 = t145;
                                                    {
                                                        void * t146 = (void *)((uint8_t *)&F.l2);
                                                        void * t147 = gm_l25;
                                                        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)t147));
                                                        void * t149 = gm_l0;
                                                        uint32_t t150 = gm_l14;
                                                        gm_t9qkh0rsooc8d t151;
                                                        GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)2ULL));
                                                        gm_b16_8 t152 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t149, t150, ((uint8_t)1));
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        void * t153 = gm_l25;
                                                        gm_th2lqbo77pvsc t154 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)t153) + 40) + 16));
                                                        gm_tl5cuhjfnc4cr t155;
                                                        GM_ST(uint8_t, (uint8_t *)&t155 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t155 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t155 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t148);
                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t155 + 16), t151);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 24), t152);
                                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t155 + 40), t154);
                                                        gm_tftb1odc739t5 t156 = gm_f_push_1e500nc_3(t146, &t155);
                                                        gm_l32 = t156;
                                                    }
                                                    uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                    if (!t157) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcm0juv8ecdls4, 302, 21);
                                                }
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            goto gm_lb10;
                                        }
gm_lb13:;
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 284, 13);
                                }
gm_lb11:;
                            }
gm_lb10:;
                            uint64_t t158 = gm_l11;
                            uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                            uint64_t t161 = r159;
                            gm_l11 = t161;
                        }
                    }
gm_lb8:;
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t163 = (uint8_t)(t162 == ((uint8_t)0ULL));
                            if (!t163) {
                                goto gm_lb29;
                            }
                            goto gm_lb28;
                        }
gm_lb29:;
                        {
                            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t165 = (uint8_t)(t164 == ((uint8_t)1ULL));
                            if (!t165) {
                                goto gm_lb30;
                            }
                            goto gm_lb28;
                        }
gm_lb30:;
                        {
                            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t167 = (uint8_t)(t166 == ((uint8_t)2ULL));
                            if (!t167) {
                                goto gm_lb31;
                            }
                            goto gm_lb28;
                        }
gm_lb31:;
                        {
                            uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t169 = (uint8_t)(t168 == ((uint8_t)3ULL));
                            if (!t169) {
                                goto gm_lb32;
                            }
                            goto gm_lb28;
                        }
gm_lb32:;
                        goto gm_lb27;
                    }
gm_lb28:;
                    void * t170 = (void *)((uint8_t *)gm_cjts5fiuldu);
                    uint64_t t171 = ((uintptr_t)((uint64_t)12ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t172;
                    GM_ST(void *, (uint8_t *)&t172, (void *)(((uint8_t *)t170 + (uintptr_t)((uint64_t)0ULL) * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(t171));
                    void * t173 = (void *)((uint8_t *)&F.l2);
                    gm_b0_1 t174 = gm_f_builtin_methods_st9oapf((void *)NULL, cx, NULL, t172, t173);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    {
                        {
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t176 = (uint8_t)(t175 == ((uint8_t)10ULL));
                            if (!t176) {
                                goto gm_lb35;
                            }
                            goto gm_lb34;
                        }
gm_lb35:;
                        {
                            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t178 = (uint8_t)(t177 == ((uint8_t)11ULL));
                            if (!t178) {
                                goto gm_lb36;
                            }
                            goto gm_lb34;
                        }
gm_lb36:;
                        goto gm_lb33;
                    }
gm_lb34:;
                    void * t179 = (void *)((uint8_t *)gm_c8g6p8jp14b);
                    uint64_t t180 = ((uintptr_t)((uint64_t)6ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(((uint8_t *)t179 + (uintptr_t)((uint64_t)0ULL) * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(t180));
                    void * t182 = (void *)((uint8_t *)&F.l2);
                    gm_b0_1 t183 = gm_f_builtin_methods_st9oapf((void *)NULL, cx, NULL, t181, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb33:;
                {
                    goto gm_lb1;
                }
gm_lb37:;
                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 276, 5);
            }
        }
gm_lb1:;
    }
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
inline __attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_variant_members_nesi2hv F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    gm_b16_8 gm_l5; /* variants */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* p */
    gm_b16_8 gm_l10; /* detail */
    GM_STACK_CHECK(&F, 605u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t13;
    gm_f_list_f0gjjbh_3((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t13;
    {
        {
            void * t14 = gm_l0;
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l11, t15, GM_CONV(gm_csguankko4lmhd, t16));
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)17ULL));
            if (!t18) {
                goto gm_lb2;
            }
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
            gm_l3 = t19;
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 1088));
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 1088) + 8));
            uint32_t t23 = gm_l3;
            uint64_t t24 = (uint64_t)t23;
            if ((uintptr_t)t24 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t24, t22, gm_srcm0juv8ecdls4, 332, 14);
            void * t25 = (void *)(((uint8_t *)t21) + (uintptr_t)t24 * 184);
            gm_l4 = t25;
            {
                {
                    void * t26 = gm_l4;
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 24) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t29 = GM_LD(gm_b16_8, (((((uint8_t *)t26) + 24) + 8) + 8));
                    gm_l5 = t29;
                    gm_b16_8 t30 = gm_l5;
                    gm_l6 = t30;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t31 = gm_l7;
                            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t33 = (t31 < t32);
                            if (!t33) {
                                goto gm_lb5;
                            }
                            {
                                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t36 = gm_l7;
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t34) + (uintptr_t)t36 * 72), 72);
                                        }
                                        {
                                            {
                                                uint8_t t37 = (uint8_t)(((((uint8_t *)&F.l8) + 16))[4] != 0);
                                                if (!t37) {
                                                    goto gm_lb11;
                                                }
                                                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                                                gm_l9 = t38;
                                                void * t39 = gm_l0;
                                                gm_t88rcucg1b7uk t40 = gm_l9;
                                                gm_b16_8 t41 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t39, t40);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tnpifv20irnph t42;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                                                gm_f_format_407qqln((void *)&gm_l12, (void *)NULL, cx, NULL, t42);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                                if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcm0juv8ecdls4, 335, 39);
                                                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                                                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                                                gm_l10 = t45;
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                            gm_b16_8 t46;
                                            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
                                            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
                                            gm_l10 = t46;
                                        }
gm_lb10:;
                                        {
                                            void * t47 = (void *)((uint8_t *)&F.l2);
                                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                            gm_b16_8 t49 = gm_l10;
                                            gm_th2lqbo77pvsc t50;
                                            memset(&t50, 0, sizeof t50);
                                            gm_t9qkh0rsooc8d t51;
                                            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)11ULL));
                                            gm_tl5cuhjfnc4cr t52;
                                            GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t48);
                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 16), t51);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 24), t49);
                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t52 + 40), t50);
                                            gm_tftb1odc739t5 t53 = gm_f_push_1e500nc_3(t47, &t52);
                                            gm_l13 = t53;
                                        }
                                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcm0juv8ecdls4, 336, 9);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 334, 5);
                                }
gm_lb8:;
                            }
gm_lb7:;
                            uint64_t t55 = gm_l7;
                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                            uint64_t t58 = r56;
                            gm_l7 = t58;
                        }
                    }
gm_lb5:;
                    memmove(gm_out, ((uint8_t *)&F.l2), 40);
                    goto gm_fdone;
                }
gm_lb4:;
                memmove(gm_out, ((uint8_t *)&F.l2), 40);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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

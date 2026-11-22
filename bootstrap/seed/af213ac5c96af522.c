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
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint8_t v1; } f1; } gm_tum2strs1ckr2;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tj2m9dbh1ekv6 v1; } f1; } gm_tij245bdr7emd;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_tum2strs1ckr2 f0; uint8_t f1; uint8_t pe[3]; } gm_ts06tb4mldv43;
_Static_assert(sizeof(gm_tum2strs1ckr2) == 8 && _Alignof(gm_tum2strs1ckr2) == 4 && offsetof(gm_tum2strs1ckr2, f0) == 0 && offsetof(gm_tum2strs1ckr2, f1) == 4, "layout of `Escape`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tij245bdr7emd) == 24 && _Alignof(gm_tij245bdr7emd) == 8 && offsetof(gm_tij245bdr7emd, f0) == 0 && offsetof(gm_tij245bdr7emd, f1) == 8, "layout of `string!IntSuffix`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts06tb4mldv43) == 12 && _Alignof(gm_ts06tb4mldv43) == 4 && offsetof(gm_ts06tb4mldv43, f0) == 0 && offsetof(gm_ts06tb4mldv43, f1) == 8, "layout of `?Escape`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_all_digits_e77c2k1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_exponent_qp4713f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_emit_char_1ips8a1 {
    gm_frame h;
    gm_ta42iqot1auae l3; /* lit */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_suffix_lh8qbg0 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_float_value_ip18r0p {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* digits */
    gm_tbci5ospk5nnn l18; /* mantissa */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_format_16f92bp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1kivsu4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3rm05av {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_51h4888 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_78j0ecp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b6aggpq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_convcvp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ek7crui {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eue6enu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g6sh9k2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ikusttp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ufd866t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v5s6luj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fraction_follows_ivbj04h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_imag_width_9gtosab {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_int_suffix_j594l00 {
    gm_frame h;
    gm_tij245bdr7emd ret;
};
struct gm_fr_int_value_cnmrbr4 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_hex_digit_9lads51 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_imag_suffix_46gu23m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_char_4qk4vjp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_escape_7pnp2v0 {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_lex_field_index_pf905g6 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_number_9j9sdfi {
    gm_frame h;
    gm_t4gut2c9fj1kt l22; /* d */
    gm_tngtuiuskn938 l24; /* value */
    gm_tbci5ospk5nnn l25; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_raw_string_uf6bd4g {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_lex_string_bcdbdjm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_b0_1 ret;
};
struct gm_fr_lex_unicode_escape_ns81oou {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_parse_valid_9fjef3j {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_utf8_bjq6ubs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_radix_name_5kts8t0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_digits_jhpsv6j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_scan_exponent_20o8k8o {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_skip_to_closing_quote_qmb6ii3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unterminated_at_line_end_v8hbf1f {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint32_t gm_f_digit_value_8ogbrqp(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4);
inline __attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_eara94q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ek7crui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eue6enu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g6sh9k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ikusttp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ufd866t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
inline __attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_j2spmhj(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_q1fs5ps(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_fbta82r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_scaled_ufcgnm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern uint8_t gm_cobksqr67t4[8] __attribute__((aligned(8)));
extern uint8_t gm_k2kjji37v2m[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmv1ea52os[40] __attribute__((aligned(8)));
extern uint8_t gm_khsmm7kmq9o[40] __attribute__((aligned(8)));
extern uint8_t gm_knasprnpdnf[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s15p9kmrsfogp[2];
extern const uint8_t gm_s32vm0u5vgiu5[18];
extern const uint8_t gm_s4hf5j4eu72mv[66];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s5frmv5tebja4[50];
extern const uint8_t gm_s5k9ll19r7m4g[4];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_s8us8sihs6eh7[74];
extern const uint8_t gm_s94a48vj74t8b[74];
extern const uint8_t gm_sae23ckujbpp9[107];
extern const uint8_t gm_safnb563bt6ue[12];
extern const uint8_t gm_sajhc6og5bv8v[59];
extern const uint8_t gm_saoa4m7dh2vl3[55];
extern const uint8_t gm_sb6jpc12sa7uj[4];
extern const uint8_t gm_sc80lo6tf645h[31];
extern const uint8_t gm_scfdk5i1nqc6a[30];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_se4629k8oa1uo[2];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_seojbs1tqr33k[46];
extern const uint8_t gm_sepo1h9bofnod[30];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sfd839bbgsa7s[63];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg550i4fp569j[55];
extern const uint8_t gm_sh6hkhojsmti9[48];
extern const uint8_t gm_sh74p5jjd1e00[4];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shq8kvmdjhmfg[4];
extern const uint8_t gm_sikg061j3im7q[2];
extern const uint8_t gm_sil0do0eri7f7[44];
extern const uint8_t gm_sjblck9bpb6r8[28];
extern const uint8_t gm_sjc9ao9066c7n[33];
extern const uint8_t gm_sjpml17j24nb4[64];
extern const uint8_t gm_skmai4uvseje8[24];
extern const uint8_t gm_skme3gpq5egsp[40];
extern const uint8_t gm_skmueji31ds5r[49];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smit2phoauonm[61];
extern const uint8_t gm_sovc43d45he5a[21];
extern const uint8_t gm_sq9ngpqr0m7ek[31];
extern const uint8_t gm_sqhghq789nvm0[8];
extern const uint8_t gm_sqj9769nr8pm0[48];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4c97p4hek1p[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srnee0q49ttns[32];
extern const uint8_t gm_ss8q42tvppqf5[79];
extern const uint8_t gm_sscmtmpbttmhb[93];
extern const uint8_t gm_su2flp9mbv542[38];
extern const uint8_t gm_su30pqbigkn5m[6];
extern const uint8_t gm_su7ft5rc766n4[3];
extern const gm_tmpl gm_tpl5gt902348obb;
extern const gm_tmpl gm_tpl6nq0lpkkblpc;
extern const gm_tmpl gm_tpl941gb7vu8kgv;
extern const gm_tmpl gm_tplbcublle8ivup;
extern const gm_tmpl gm_tpli3i5m2srtl1a;
extern const gm_tmpl gm_tplj7j0vuugbh2u;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tplku5ga9b4h0i9;
extern const gm_tmpl gm_tpll6pilv9krmhr;
extern const gm_tmpl gm_tplm61d8mlvmepm;
extern const gm_tmpl gm_tplopnhfma5is23;
extern const gm_tmpl gm_tplqjo10upi8bs6;
extern const gm_tmpl gm_tplrcmh11f2ocju;
extern const gm_tmpl gm_tpls85ptecv5rkj;
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
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_b4v8vul F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    uint64_t t5 = gm_l1;
    uint8_t t6 = (t5 < t4);
    if (t6) {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t5 * 1));
        gm_tqs5p4n2tq19v t8;
        GM_ST(uint8_t, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[1] = 1;
        gm_l2 = t8;
    } else {
        gm_tqs5p4n2tq19v t9;
        memset(&t9, 0, sizeof t9);
        gm_l2 = t9;
    }
    gm_tqs5p4n2tq19v t10 = gm_l2;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_current_char_ctktsl2 F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
    gm_tb31k5l794t7p t6 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t3, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
gm_fprop:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_error_p5r0d56 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    uint64_t t6 = gm_l2;
    gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
    gm_b16_8 t8 = gm_l3;
    gm_b0_1 t9 = gm_f_report_eardf07((void *)NULL, cx, NULL, t3, t7, t8);
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
inline __attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_16f92bp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skme3gpq5egsp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbcublle8ivup), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1kivsu4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss8q42tvppqf5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(78));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm61d8mlvmepm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3rm05av F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sscmtmpbttmhb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(92));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll6pilv9krmhr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_51h4888 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjc9ao9066c7n));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj7j0vuugbh2u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_78j0ecp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh74p5jjd1e00));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls85ptecv5rkj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_b6aggpq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sepo1h9bofnod));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplopnhfma5is23), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ek7crui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ek7crui F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su2flp9mbv542));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl941gb7vu8kgv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eue6enu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_eue6enu F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scfdk5i1nqc6a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli3i5m2srtl1a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g6sh9k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g6sh9k2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seojbs1tqr33k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5gt902348obb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ikusttp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ikusttp F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmueji31ds5r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrcmh11f2ocju), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ufd866t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0) {
    struct gm_fr_format_ufd866t F;
    gm_tm98ap4112hee gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tm98ap4112hee t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s94a48vj74t8b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
    gm_f_format_eara94q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqjo10upi8bs6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v5s6luj F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8us8sihs6eh7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplku5ga9b4h0i9), t3);
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
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2) {
    struct gm_fr_int_value_cnmrbr4 F;
    void * gm_l0; /* lx */
    gm_t2lqu8jsj511d gm_l1; /* digits */
    uint32_t gm_l2; /* radix */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t7 = (uintptr_t)t5;
    uint64_t t8 = (uintptr_t)t6;
    if (t7 > t8 || t8 > (uintptr_t)t4) gm_trap_slice(t7, t8, t4, gm_srct4c97p4hek1p, 226, 24);
    uint64_t t9 = ((uintptr_t)t6) - (uintptr_t)t5;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t3 + (uintptr_t)t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
    uint32_t t11 = gm_l2;
    gm_f_parse_valid_9fjef3j((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t10, t11);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_2hoggnj F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_9lads51 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_digit_2hoggnj(t2);
    if (t3) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 >= ((uint8_t)97ULL));
        if (t5) {
            uint8_t t6 = gm_l0;
            uint8_t t7 = (t6 <= ((uint8_t)102ULL));
            gm_l3 = t7;
        } else {
            gm_l3 = ((uint8_t)0);
        }
        uint8_t t8 = gm_l3;
        gm_l2 = t8;
    }
    uint8_t t9 = gm_l2;
    if (t9) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t10 = gm_l0;
        uint8_t t11 = (t10 >= ((uint8_t)65ULL));
        if (t11) {
            uint8_t t12 = gm_l0;
            uint8_t t13 = (t12 <= ((uint8_t)70ULL));
            gm_l4 = t13;
        } else {
            gm_l4 = ((uint8_t)0);
        }
        uint8_t t14 = gm_l4;
        gm_l1 = t14;
    }
    uint8_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_imag_suffix_46gu23m F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sikg061j3im7q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sb6jpc12sa7uj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_shq8kvmdjhmfg));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s5k9ll19r7m4g));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
    gm_t0uuhaljsf410 t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 32), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 48), t6);
    uint8_t t8 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 0, ((uint8_t *)&t2))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 16, ((uint8_t *)&t2))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 32, ((uint8_t *)&t2))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 48, ((uint8_t *)&t2))) t8 = 1;
    uint8_t t9 = (uint8_t)(t8);
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lone_cr_ktb0jvm F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 198, 22);
    uint64_t t9 = r7;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)gm_c644mb9o5bq));
    gm_b0_1 t11 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t2, t4, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
    uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 199, 5);
    uint64_t t17 = r15;
    GM_ST(uint64_t, (((uint8_t *)t12) + 24), t17);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint8_t t7 = (t6 < t4);
    if (t7) {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
        gm_tqs5p4n2tq19v t9;
        GM_ST(uint8_t, ((uint8_t *)&t9), t8);
        ((uint8_t *)&t9)[1] = 1;
        gm_l1 = t9;
    } else {
        gm_tqs5p4n2tq19v t10;
        memset(&t10, 0, sizeof t10);
        gm_l1 = t10;
    }
    gm_tqs5p4n2tq19v t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_at_74p8lkq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t10 = r8;
    uint8_t t11 = (t10 < t4);
    if (t11) {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t10 * 1));
        gm_tqs5p4n2tq19v t13;
        GM_ST(uint8_t, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[1] = 1;
        gm_l2 = t13;
    } else {
        gm_tqs5p4n2tq19v t14;
        memset(&t14, 0, sizeof t14);
        gm_l2 = t14;
    }
    gm_tqs5p4n2tq19v t15 = gm_l2;
    F.ret = t15;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint32_t t4 = (uint32_t)t3;
    uint64_t t5 = gm_l2;
    uint32_t t6 = (uint32_t)t5;
    gm_tl874t2atnlh3 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 8), t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_tg7cev6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l2;
    gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t3, t4, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_digits_e77c2k1 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t7 = gm_l0;
    gm_l1 = t7;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t13 = gm_l2;
                {
                    {
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
                        gm_l3 = t14;
                        uint8_t t15 = gm_l3;
                        uint8_t t16 = gm_f_is_digit_2hoggnj(t15);
                        uint8_t t17 = (uint8_t)!t16;
                        if (t17) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 515, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l2;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l2 = t21;
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
inline __attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_exponent_qp4713f F;
    void * gm_l0; /* lx */
    gm_b16_8 gm_l1; /* markers */
    uint8_t gm_l2; /* m */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l5;
    gm_tqs5p4n2tq19v gm_l6;
    void * t2 = gm_l0;
    gm_tqs5p4n2tq19v t3 = gm_f_peek_72bostn(t2);
    gm_l5 = t3;
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
        gm_l2 = t5;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint8_t t6 = gm_l2;
    gm_b16_8 t7 = gm_l1;
    uint8_t t8 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t7) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t7))) + i * 1, ((uint8_t *)&t6))) { t8 = 1; break; }
    uint8_t t9 = (uint8_t)(!t8);
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l3 = ((uint64_t)1ULL);
    void * t10 = gm_l0;
    uint64_t t11 = gm_l3;
    gm_tqs5p4n2tq19v t12 = gm_f_peek_at_74p8lkq(t10, t11);
    gm_tqs5p4n2tq19v t13;
    GM_ST(uint8_t, ((uint8_t *)&t13), ((uint8_t)43ULL));
    ((uint8_t *)&t13)[1] = 1;
    gm_tqs5p4n2tq19v t14;
    GM_ST(uint8_t, ((uint8_t *)&t14), ((uint8_t)45ULL));
    ((uint8_t *)&t14)[1] = 1;
    gm_t5r6k0fdcfsb3 t15;
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t15 + 0), t13);
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t15 + 2), t14);
    uint8_t t16 = 0;
    if (gm_eqb44cb3cf1n(((uint8_t *)&t15) + 0, ((uint8_t *)&t12))) t16 = 1;
    if (gm_eqb44cb3cf1n(((uint8_t *)&t15) + 2, ((uint8_t *)&t12))) t16 = 1;
    uint8_t t17 = (uint8_t)(t16);
    if (t17) {
        uint64_t t18 = gm_l3;
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 192, 38);
        uint64_t t21 = r19;
        gm_l3 = t21;
    }
    void * t22 = gm_l0;
    uint64_t t23 = gm_l3;
    gm_tqs5p4n2tq19v t24 = gm_f_peek_at_74p8lkq(t22, t23);
    gm_l6 = t24;
    uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
    if (t25) {
        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
        gm_l4 = t26;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint8_t t27 = gm_l4;
    uint8_t t28 = gm_f_is_digit_2hoggnj(t27);
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_emit_char_1ips8a1 F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* value */
    uint64_t gm_l2; /* lo */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_tbci5ospk5nnn t4;
        gm_f_from_u64_ud13q5v((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t46lmulm8gaj3 t5;
        memset(&t5, 0, sizeof t5);
        GM_ST(uint8_t, (uint8_t *)&F.l3 + 61, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l3 + 62, 0);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3 + 0), t4);
        GM_ST(gm_t46lmulm8gaj3, ((uint8_t *)&F.l3 + 48), t5);
        GM_ST(uint8_t, ((uint8_t *)&F.l3 + 60), ((uint8_t)1));
    }
    {
        void * t6 = gm_l0;
        gm_tq7s98ihonkju t7 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &(*(gm_ta42iqot1auae *)((uint8_t *)&F.l3)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmv1ea52os)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t7;
        uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srct4c97p4hek1p, 321, 18);
        void * t9 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
        uint64_t t10 = gm_l2;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 24));
        gm_t3fcbuehh3bs2 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t13 + 8), t9);
        gm_b0_1 t14 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t6, &t13, t10, t12);
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_suffix_lh8qbg0 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            if (!t9) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t11;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[1] = 1;
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t12 = gm_l0;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
            uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t16;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[1] = 1;
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t17 = gm_l0;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (!t19) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t21;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[1] = 1;
            gm_l1 = t21;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_taari1s0m51a7 t22;
            memset(&t22, 0, sizeof t22);
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 556, 50);
    }
gm_lb1:;
    gm_taari1s0m51a7 t23 = gm_l1;
    F.ret = t23;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4) {
    struct gm_fr_float_value_ip18r0p F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    gm_t2lqu8jsj511d gm_l2; /* int_part */
    gm_tc70a9dbbmgkg gm_l3; /* frac_part */
    int64_t gm_l4; /* exp */
    gm_t2lqu8jsj511d gm_l5; /* frac */
    gm_b16_8 gm_l6; /* frac_text */
    int64_t gm_l7; /* frac_digits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l10; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* c */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint8_t gm_l17; /* c */
    GM_STACK_CHECK(&F, 857u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t17) {
        gm_t2lqu8jsj511d t18 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l3));
        gm_l5 = t18;
    } else {
        gm_t2lqu8jsj511d t19;
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t19 + 8), ((uint64_t)0ULL));
        gm_l5 = t19;
    }
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 8) + 8));
    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t25 = (uintptr_t)t23;
    uint64_t t26 = (uintptr_t)t24;
    if (t25 > t26 || t26 > (uintptr_t)t22) gm_trap_slice(t25, t26, t22, gm_srct4c97p4hek1p, 238, 18);
    uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)t23;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + (uintptr_t)t23 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
    gm_l6 = t28;
    gm_l7 = ((int64_t)0LL);
    gm_b16_8 t29 = gm_l6;
    gm_l8 = t29;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l9;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t35 = gm_l9;
                {
                    {
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
                        gm_l10 = t36;
                        uint8_t t37 = gm_l10;
                        uint8_t t38 = (!(t37 == ((uint8_t)95ULL)));
                        if (t38) {
                            int64_t t39 = gm_l7;
                            int64_t r40; bool o41 = __builtin_add_overflow(t39, ((int64_t)1LL), &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 241, 27);
                            int64_t t42 = r40;
                            gm_l7 = t42;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 240, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t43 = gm_l9;
            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
            uint64_t t46 = r44;
            gm_l9 = t46;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t47;
        gm_f_list_uvfu1k1((void *)&t47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t47;
    }
    void * t48 = gm_l0;
    void * t49 = GM_LD(void *, (((uint8_t *)t48) + 8));
    uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 8) + 8));
    uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t53 = (uintptr_t)t51;
    uint64_t t54 = (uintptr_t)t52;
    if (t53 > t54 || t54 > (uintptr_t)t50) gm_trap_slice(t53, t54, t50, gm_srct4c97p4hek1p, 244, 14);
    uint64_t t55 = ((uintptr_t)t52) - (uintptr_t)t51;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t49 + (uintptr_t)t51 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
    gm_l12 = t56;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t57 = gm_l13;
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t59 = (t57 < t58);
            if (!t59) {
                goto gm_lb6;
            }
            {
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t62 = gm_l13;
                {
                    {
                        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t60) + (uintptr_t)t62 * 1));
                        gm_l14 = t63;
                        uint8_t t64 = gm_l14;
                        uint8_t t65 = (!(t64 == ((uint8_t)95ULL)));
                        if (t65) {
                            void * t66 = (void *)((uint8_t *)&F.l11);
                            uint8_t t67 = gm_l14;
                            gm_tftb1odc739t5 t68 = gm_f_push_t83rg2m(t66, t67);
                            gm_l19 = t68;
                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srct4c97p4hek1p, 245, 27);
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 244, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t70 = gm_l13;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l13 = t73;
        }
    }
gm_lb6:;
    gm_b16_8 t74 = gm_l6;
    gm_l15 = t74;
    gm_l16 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t75 = gm_l16;
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            uint8_t t77 = (t75 < t76);
            if (!t77) {
                goto gm_lb11;
            }
            {
                void * t78 = GM_LD(void *, ((uint8_t *)&gm_l15));
                uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint64_t t80 = gm_l16;
                {
                    {
                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t78) + (uintptr_t)t80 * 1));
                        gm_l17 = t81;
                        uint8_t t82 = gm_l17;
                        uint8_t t83 = (!(t82 == ((uint8_t)95ULL)));
                        if (t83) {
                            void * t84 = (void *)((uint8_t *)&F.l11);
                            uint8_t t85 = gm_l17;
                            gm_tftb1odc739t5 t86 = gm_f_push_t83rg2m(t84, t85);
                            gm_l20 = t86;
                            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srct4c97p4hek1p, 248, 27);
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 247, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t88 = gm_l16;
            uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
            uint64_t t91 = r89;
            gm_l16 = t91;
        }
    }
gm_lb11:;
    {
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        uint32_t t94 = gm_l1;
        gm_f_parse_valid_9fjef3j((void *)&F.l18, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint32_t t95 = gm_l1;
    uint8_t t96 = ((t95 == ((uint32_t)16ULL)));
    if (t96) {
        int64_t t97 = gm_l4;
        int64_t t98 = gm_l7;
        int64_t r99; bool o100 = __builtin_mul_overflow(((int64_t)4LL), t98, &r99);
        if (o100) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 252, 61);
        int64_t t101 = r99;
        int64_t r102; bool o103 = __builtin_sub_overflow(t97, t101, &r102);
        if (o103) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 252, 55);
        int64_t t104 = r102;
        gm_tbci5ospk5nnn t105 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l18));
        gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t105, ((uint32_t)2ULL), t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        int64_t t106 = gm_l4;
        int64_t t107 = gm_l7;
        int64_t r108; bool o109 = __builtin_sub_overflow(t106, t107, &r108);
        if (o109) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 253, 39);
        int64_t t110 = r108;
        gm_tbci5ospk5nnn t111 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l18));
        gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t111, ((uint32_t)10ULL), t110);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fraction_follows_ivbj04h F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* first_is_digit */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    {
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_l2;
            gm_tqs5p4n2tq19v t9 = gm_f_byte_b4v8vul(t7, t8);
            gm_l6 = t9;
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
            gm_l3 = t11;
            uint8_t t12 = gm_l3;
            uint8_t t13 = gm_f_is_digit_2hoggnj(t12);
            gm_l4 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        gm_l4 = ((uint8_t)0);
    }
gm_lb1:;
    uint32_t t14 = gm_l1;
    uint8_t t15 = ((t14 == ((uint32_t)10ULL)));
    if (t15) {
        uint8_t t16 = gm_l4;
        F.ret = t16;
        goto gm_fret;
    }
    uint64_t t17 = gm_l2;
    gm_l5 = t17;
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t19) + 8) + 8));
            uint8_t t21 = (t18 < t20);
            if (!t21) {
                goto gm_lb3;
            }
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 8));
            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 8) + 8));
            uint64_t t25 = gm_l5;
            if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_srct4c97p4hek1p, 148, 50);
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)t25 * 1));
            uint8_t t27 = gm_f_is_hex_digit_9lads51(t26);
            if (t27) {
                gm_l7 = ((uint8_t)1);
            } else {
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 8));
                uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 8) + 8));
                uint64_t t31 = gm_l5;
                if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srct4c97p4hek1p, 148, 64);
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)t31 * 1));
                uint8_t t33 = ((t32 == ((uint8_t)95ULL)));
                gm_l7 = t33;
            }
            uint8_t t34 = gm_l7;
            if (!t34) {
                goto gm_lb3;
            }
            {
                uint64_t t35 = gm_l5;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 148, 84);
                uint64_t t38 = r36;
                gm_l5 = t38;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    uint8_t t39 = gm_l4;
    if (t39) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l5;
        gm_tqs5p4n2tq19v t42 = gm_f_byte_b4v8vul(t40, t41);
        gm_tqs5p4n2tq19v t43;
        GM_ST(uint8_t, ((uint8_t *)&t43), ((uint8_t)112ULL));
        ((uint8_t *)&t43)[1] = 1;
        uint8_t t44 = (gm_eqb44cb3cf1n(((uint8_t *)&t42), ((uint8_t *)&t43)));
        gm_l8 = t44;
    }
    uint8_t t45 = gm_l8;
    F.ret = t45;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_imag_width_9gtosab F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sb6jpc12sa7uj));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            if (!t9) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t11;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[1] = 1;
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t12 = gm_l0;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_shq8kvmdjhmfg));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
            uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t16;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[1] = 1;
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t17 = gm_l0;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s5k9ll19r7m4g));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (!t19) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t21;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[1] = 1;
            gm_l1 = t21;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_taari1s0m51a7 t22;
            memset(&t22, 0, sizeof t22);
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 549, 48);
    }
gm_lb1:;
    gm_taari1s0m51a7 t23 = gm_l1;
    F.ret = t23;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_int_suffix_j594l00 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* digits */
    uint32_t gm_l2; /* width */
    uint8_t gm_l3; /* valid */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    uint32_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    uint8_t gm_l11;
    gm_taari1s0m51a7 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 t12 = gm_l0;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s6groq37ram3h));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(5));
    uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
    if (t14) {
        gm_tj2m9dbh1ekv6 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
        GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t15);
        goto gm_fdone;
    }
    gm_b16_8 t16 = gm_l0;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s0i3i86nphpsh));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
    uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
    if (t18) {
        gm_tj2m9dbh1ekv6 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
        GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t19);
        goto gm_fdone;
    }
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t21) gm_trap_slice(t22, t23, t21, gm_srct4c97p4hek1p, 525, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    gm_l1 = t25;
    {
        {
            void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t27, gm_srct4c97p4hek1p, 526, 8);
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 1));
            gm_tqs5p4n2tq19v t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t29 + 1), ((uint8_t)105ULL));
            uint8_t t30 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 0, ((uint8_t *)&t28))) t30 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 1, ((uint8_t *)&t28))) t30 = 1;
            uint8_t t31 = (uint8_t)(t30);
            if (!t31) {
                goto gm_lb2;
            }
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t33 = (t32 > ((uint64_t)0ULL));
            if (!t33) {
                goto gm_lb2;
            }
            gm_b16_8 t34 = gm_l1;
            uint8_t t35 = gm_f_all_digits_e77c2k1(t34);
            if (!t35) {
                goto gm_lb2;
            }
            gm_l2 = ((uint32_t)0ULL);
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t37 = (t36 <= ((uint64_t)3ULL));
            if (t37) {
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t41 = (!(t40 == ((uint8_t)48ULL)));
                gm_l8 = t41;
            } else {
                gm_l8 = ((uint8_t)0);
            }
            uint8_t t42 = gm_l8;
            gm_l3 = t42;
            uint8_t t43 = gm_l3;
            if (t43) {
                gm_b16_8 t44 = gm_l1;
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
                                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t48) + (uintptr_t)t50 * 1));
                                    gm_l6 = t51;
                                    uint32_t t52 = gm_l2;
                                    uint32_t r53; bool o54 = __builtin_mul_overflow(t52, ((uint32_t)10ULL), &r53);
                                    if (o54) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 530, 39);
                                    uint32_t t55 = r53;
                                    uint8_t t56 = gm_l6;
                                    uint8_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint8_t)48ULL), &r57);
                                    if (o58) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 530, 56);
                                    uint8_t t59 = r57;
                                    uint32_t t60 = (uint32_t)t59;
                                    uint32_t r61; bool o62 = __builtin_add_overflow(t55, t60, &r61);
                                    if (o62) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 530, 39);
                                    uint32_t t63 = r61;
                                    gm_l2 = t63;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 530, 13);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint64_t t64 = gm_l5;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l5 = t67;
                    }
                }
gm_lb3:;
                uint32_t t68 = gm_l2;
                gm_l7 = t68;
                uint32_t t69 = gm_l7;
                uint8_t t70 = (((uint32_t)1ULL) <= t69);
                if (t70) {
                    uint32_t t71 = gm_l7;
                    uint8_t t72 = (t71 <= ((uint32_t)128ULL));
                    gm_l9 = t72;
                } else {
                    gm_l9 = ((uint8_t)0);
                }
                uint8_t t73 = gm_l9;
                gm_l3 = t73;
            }
            uint8_t t74 = gm_l3;
            uint8_t t75 = (uint8_t)!t74;
            if (t75) {
                {
                    gm_b16_8 t76 = gm_l0;
                    gm_tnpifv20irnph t77;
                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                    gm_f_format_1kivsu4((void *)&gm_l10, (void *)NULL, cx, NULL, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srct4c97p4hek1p, 534, 21);
                {
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t80);
                    goto gm_fdone;
                }
            }
            void * t81 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t82, gm_srct4c97p4hek1p, 536, 12);
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)t81) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t84 = ((t83 == ((uint8_t)117ULL)));
            if (t84) {
                uint32_t t85 = gm_l2;
                gm_tj2m9dbh1ekv6 t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t86 + 4), t85);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
                ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
                GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t86);
                goto gm_fdone;
            }
            uint32_t t87 = gm_l2;
            gm_tj2m9dbh1ekv6 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t88 + 4), t87);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t88);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t89 = gm_l0;
    gm_taari1s0m51a7 t90 = gm_f_float_suffix_lh8qbg0(t89);
    gm_l12 = t90;
    {
        {
            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l12))[1] != 0);
            if (t91) {
                goto gm_lb9;
            }
            gm_l11 = ((uint8_t)0);
            goto gm_lb8;
        }
gm_lb9:;
        {
            gm_l11 = ((uint8_t)1);
            goto gm_lb8;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 539, 8);
    }
gm_lb8:;
    uint8_t t92 = gm_l11;
    if (t92) {
        {
            gm_b16_8 t93 = gm_l0;
            gm_tnpifv20irnph t94;
            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
            gm_f_format_3rm05av((void *)&gm_l13, (void *)NULL, cx, NULL, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srct4c97p4hek1p, 540, 17);
        {
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t97);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t98 = gm_l0;
        gm_tnpifv20irnph t99;
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t98);
        gm_f_format_16f92bp((void *)&gm_l14, (void *)NULL, cx, NULL, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srct4c97p4hek1p, 542, 13);
    {
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8), t102);
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
extern __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_char_4qk4vjp F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint32_t gm_l2; /* value */
    uint8_t gm_l3; /* b */
    uint32_t gm_l4; /* c */
    uint8_t gm_l5; /* x */
    gm_tb31k5l794t7p gm_l6; /* c */
    GM_STACK_CHECK(&F, 653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l7;
    gm_ts06tb4mldv43 gm_l8;
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    gm_l1 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 24));
    uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 261, 5);
    uint64_t t13 = r11;
    GM_ST(uint64_t, (((uint8_t *)t8) + 24), t13);
    gm_l2 = ((uint32_t)0ULL);
    void * t14 = gm_l0;
    gm_tqs5p4n2tq19v t15 = gm_f_peek_72bostn(t14);
    gm_l7 = t15;
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
    if (t16) {
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
        gm_l3 = t17;
    } else {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l1;
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 24));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sq9ngpqr0m7ek));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(30));
        gm_b0_1 t23 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t18, t19, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t24 = gm_l0;
        uint64_t t25 = gm_l1;
        gm_b0_1 t26 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t24, ((uint32_t)0ULL), t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint8_t t27 = gm_l3;
    gm_tqs5p4n2tq19v t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t28 + 1), ((uint8_t)13ULL));
    uint8_t t29 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 0, ((uint8_t *)&t27))) t29 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 1, ((uint8_t *)&t27))) t29 = 1;
    uint8_t t30 = (uint8_t)(t29);
    if (t30) {
        void * t31 = gm_l0;
        uint64_t t32 = gm_l1;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 24));
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sq9ngpqr0m7ek));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(30));
        gm_b0_1 t36 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t31, t32, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t37 = gm_l0;
        uint64_t t38 = gm_l1;
        gm_b0_1 t39 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t37, ((uint32_t)0ULL), t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint8_t t40 = gm_l3;
    uint8_t t41 = ((t40 == ((uint8_t)39ULL)));
    if (t41) {
        void * t42 = gm_l0;
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 24));
        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 274, 9);
        uint64_t t47 = r45;
        GM_ST(uint64_t, (((uint8_t *)t42) + 24), t47);
        void * t48 = gm_l0;
        uint64_t t49 = gm_l1;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 24));
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_skmai4uvseje8));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(23));
        gm_b0_1 t53 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t48, t49, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t54 = gm_l0;
        uint64_t t55 = gm_l1;
        gm_b0_1 t56 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t54, ((uint32_t)0ULL), t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint8_t t57 = gm_l3;
    uint8_t t58 = ((t57 == ((uint8_t)92ULL)));
    if (t58) {
        void * t59 = gm_l0;
        gm_ts06tb4mldv43 t60 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t60;
        {
            {
                uint8_t t61 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                if (!t61) {
                    goto gm_lb2;
                }
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                if (!t63) {
                    goto gm_lb2;
                }
                uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
                gm_l4 = t64;
                uint32_t t65 = gm_l4;
                gm_l2 = t65;
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                if (!t66) {
                    goto gm_lb3;
                }
                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                uint8_t t68 = (uint8_t)(t67 == ((uint8_t)1ULL));
                if (!t68) {
                    goto gm_lb3;
                }
                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 4));
                gm_l5 = t69;
                uint8_t t70 = gm_l5;
                uint32_t t71 = (uint32_t)t70;
                gm_l2 = t71;
                goto gm_lb1;
            }
gm_lb3:;
            {
                uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                if (t72) {
                    goto gm_lb4;
                }
                void * t73 = gm_l0;
                uint8_t t74 = gm_f_skip_to_closing_quote_qmb6ii3(t73);
                void * t75 = gm_l0;
                uint64_t t76 = gm_l1;
                gm_b0_1 t77 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t75, ((uint32_t)0ULL), t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 280, 9);
        }
gm_lb1:;
    } else {
        void * t78 = gm_l0;
        gm_tb31k5l794t7p t79 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t78));
        gm_l6 = t79;
        void * t80 = gm_l0;
        void * t81 = gm_l0;
        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 24));
        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint64_t r84; bool o85 = __builtin_add_overflow(t82, t83, &r84);
        if (o85) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 291, 9);
        uint64_t t86 = r84;
        GM_ST(uint64_t, (((uint8_t *)t80) + 24), t86);
        uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
        gm_l2 = t87;
    }
    void * t88 = gm_l0;
    gm_tqs5p4n2tq19v t89 = gm_f_peek_72bostn(t88);
    gm_tqs5p4n2tq19v t90;
    GM_ST(uint8_t, ((uint8_t *)&t90), ((uint8_t)39ULL));
    ((uint8_t *)&t90)[1] = 1;
    uint8_t t91 = (gm_eqb44cb3cf1n(((uint8_t *)&t89), ((uint8_t *)&t90)));
    if (t91) {
        void * t92 = gm_l0;
        void * t93 = gm_l0;
        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 24));
        uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
        if (o96) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 295, 9);
        uint64_t t97 = r95;
        GM_ST(uint64_t, (((uint8_t *)t92) + 24), t97);
    } else {
        void * t98 = gm_l0;
        uint8_t t99 = gm_f_skip_to_closing_quote_qmb6ii3(t98);
        if (t99) {
            void * t100 = gm_l0;
            uint64_t t101 = gm_l1;
            void * t102 = gm_l0;
            uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 24));
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_saoa4m7dh2vl3));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(54));
            gm_b0_1 t105 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t100, t101, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t106 = gm_l0;
            uint64_t t107 = gm_l1;
            void * t108 = gm_l0;
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 24));
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sq9ngpqr0m7ek));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(30));
            gm_b0_1 t111 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t106, t107, t109, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    void * t112 = gm_l0;
    uint32_t t113 = gm_l2;
    uint64_t t114 = gm_l1;
    gm_b0_1 t115 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t112, t113, t114);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_escape_7pnp2v0 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* b */
    gm_tqs5p4n2tq19v gm_l3; /* simple */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* hi */
    uint8_t gm_l6; /* low */
    gm_tb31k5l794t7p gm_l7; /* c */
    GM_STACK_CHECK(&F, 914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l8;
    gm_tqs5p4n2tq19v gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t28 = gm_l0;
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 24));
    gm_l1 = t29;
    void * t30 = gm_l0;
    void * t31 = gm_l0;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
    if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 331, 5);
    uint64_t t35 = r33;
    GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
    void * t36 = gm_l0;
    gm_tqs5p4n2tq19v t37 = gm_f_peek_72bostn(t36);
    gm_l8 = t37;
    uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
    if (t38) {
        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
        gm_l2 = t39;
    } else {
        gm_ts06tb4mldv43 t40;
        memset(&t40, 0, sizeof t40);
        F.ret = t40;
        goto gm_fret;
    }
    {
        {
            uint8_t t41 = gm_l2;
            uint8_t t42 = ((t41 == ((uint8_t)110ULL)));
            if (!t42) {
                goto gm_lb2;
            }
            gm_tqs5p4n2tq19v t43;
            GM_ST(uint8_t, ((uint8_t *)&t43), ((uint8_t)10ULL));
            ((uint8_t *)&t43)[1] = 1;
            gm_l3 = t43;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t44 = gm_l2;
            uint8_t t45 = ((t44 == ((uint8_t)114ULL)));
            if (!t45) {
                goto gm_lb3;
            }
            gm_tqs5p4n2tq19v t46;
            GM_ST(uint8_t, ((uint8_t *)&t46), ((uint8_t)13ULL));
            ((uint8_t *)&t46)[1] = 1;
            gm_l3 = t46;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t47 = gm_l2;
            uint8_t t48 = ((t47 == ((uint8_t)116ULL)));
            if (!t48) {
                goto gm_lb4;
            }
            gm_tqs5p4n2tq19v t49;
            GM_ST(uint8_t, ((uint8_t *)&t49), ((uint8_t)9ULL));
            ((uint8_t *)&t49)[1] = 1;
            gm_l3 = t49;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t50 = gm_l2;
            uint8_t t51 = ((t50 == ((uint8_t)48ULL)));
            if (!t51) {
                goto gm_lb5;
            }
            gm_tqs5p4n2tq19v t52;
            GM_ST(uint8_t, ((uint8_t *)&t52), ((uint8_t)0ULL));
            ((uint8_t *)&t52)[1] = 1;
            gm_l3 = t52;
            goto gm_lb1;
        }
gm_lb5:;
        {
            {
                {
                    uint8_t t53 = gm_l2;
                    uint8_t t54 = ((t53 == ((uint8_t)92ULL)));
                    if (!t54) {
                        goto gm_lb8;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t55 = gm_l2;
                    uint8_t t56 = ((t55 == ((uint8_t)34ULL)));
                    if (!t56) {
                        goto gm_lb9;
                    }
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t57 = gm_l2;
                    uint8_t t58 = ((t57 == ((uint8_t)39ULL)));
                    if (!t58) {
                        goto gm_lb10;
                    }
                    goto gm_lb7;
                }
gm_lb10:;
                goto gm_lb6;
            }
gm_lb7:;
            uint8_t t59 = gm_l2;
            gm_tqs5p4n2tq19v t60;
            GM_ST(uint8_t, ((uint8_t *)&t60), t59);
            ((uint8_t *)&t60)[1] = 1;
            gm_l3 = t60;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_tqs5p4n2tq19v t61;
            memset(&t61, 0, sizeof t61);
            gm_l3 = t61;
            goto gm_lb1;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 333, 19);
    }
gm_lb1:;
    {
        {
            uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
            if (!t62) {
                goto gm_lb13;
            }
            uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            gm_l4 = t63;
            void * t64 = gm_l0;
            void * t65 = gm_l0;
            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 24));
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            if (o68) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 342, 9);
            uint64_t t69 = r67;
            GM_ST(uint64_t, (((uint8_t *)t64) + 24), t69);
            uint8_t t70 = gm_l4;
            uint32_t t71 = (uint32_t)t70;
            gm_tum2strs1ckr2 t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t72 + 4), t71);
            gm_ts06tb4mldv43 t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 10, 0);
            GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t73), t72);
            ((uint8_t *)&t73)[8] = 1;
            F.ret = t73;
            goto gm_fret;
        }
gm_lb13:;
    }
gm_lb12:;
    {
        {
            uint8_t t74 = gm_l2;
            uint8_t t75 = ((t74 == ((uint8_t)120ULL)));
            if (!t75) {
                goto gm_lb15;
            }
            void * t76 = gm_l0;
            void * t77 = gm_l0;
            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 24));
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            if (o80) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 347, 13);
            uint64_t t81 = r79;
            GM_ST(uint64_t, (((uint8_t *)t76) + 24), t81);
            void * t82 = gm_l0;
            gm_tqs5p4n2tq19v t83 = gm_f_peek_72bostn(t82);
            gm_l9 = t83;
            uint8_t t84 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
            if (t84) {
                uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                gm_l5 = t85;
            } else {
                gm_l5 = ((uint8_t)0ULL);
            }
            void * t86 = gm_l0;
            gm_tqs5p4n2tq19v t87 = gm_f_peek_at_74p8lkq(t86, ((uint64_t)1ULL));
            gm_l10 = t87;
            uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
            if (t88) {
                uint8_t t89 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                gm_l6 = t89;
            } else {
                gm_l6 = ((uint8_t)0ULL);
            }
            {
                {
                    uint8_t t90 = gm_l5;
                    uint8_t t91 = gm_f_is_hex_digit_9lads51(t90);
                    if (!t91) {
                        goto gm_lb17;
                    }
                    uint8_t t92 = gm_l6;
                    uint8_t t93 = gm_f_is_hex_digit_9lads51(t92);
                    if (!t93) {
                        goto gm_lb17;
                    }
                    void * t94 = gm_l0;
                    void * t95 = gm_l0;
                    uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 24));
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)2ULL), &r97);
                    if (o98) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 351, 17);
                    uint64_t t99 = r97;
                    GM_ST(uint64_t, (((uint8_t *)t94) + 24), t99);
                    uint8_t t100 = gm_l5;
                    uint32_t t101 = gm_f_digit_value_8ogbrqp(t100);
                    uint32_t r102; bool o103 = __builtin_mul_overflow(t101, ((uint32_t)16ULL), &r102);
                    if (o103) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 352, 31);
                    uint32_t t104 = r102;
                    uint8_t t105 = gm_l6;
                    uint32_t t106 = gm_f_digit_value_8ogbrqp(t105);
                    uint32_t r107; bool o108 = __builtin_add_overflow(t104, t106, &r107);
                    if (o108) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 352, 31);
                    uint32_t t109 = r107;
                    uint8_t t110 = (uint8_t)t109;
                    gm_tum2strs1ckr2 t111;
                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                    GM_ST(uint8_t, (uint8_t *)&t111 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 6, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)1ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 4), t110);
                    gm_ts06tb4mldv43 t112;
                    GM_ST(uint8_t, (uint8_t *)&t112 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t112 + 10, 0);
                    GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t112), t111);
                    ((uint8_t *)&t112)[8] = 1;
                    F.ret = t112;
                    goto gm_fret;
                }
gm_lb17:;
            }
gm_lb16:;
            {
                for (;;) {
                    void * t113 = gm_l0;
                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)t113) + 24));
                    uint64_t t115 = gm_l1;
                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)4ULL), &r116);
                    if (o117) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 28);
                    uint64_t t118 = r116;
                    uint8_t t119 = (t114 < t118);
                    if (!t119) {
                        goto gm_lb18;
                    }
                    void * t120 = gm_l0;
                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)t120) + 24));
                    void * t122 = gm_l0;
                    uint64_t t123 = GM_LD(uint64_t, ((((uint8_t *)t122) + 8) + 8));
                    uint8_t t124 = (t121 < t123);
                    if (!t124) {
                        goto gm_lb18;
                    }
                    void * t125 = gm_l0;
                    void * t126 = GM_LD(void *, (((uint8_t *)t125) + 8));
                    uint64_t t127 = GM_LD(uint64_t, ((((uint8_t *)t125) + 8) + 8));
                    void * t128 = gm_l0;
                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 24));
                    if ((uintptr_t)t129 >= (uintptr_t)(t127)) gm_trap_index((uintptr_t)t129, t127, gm_srct4c97p4hek1p, 354, 82);
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)t126) + (uintptr_t)t129 * 1));
                    uint8_t t131 = gm_f_is_hex_digit_9lads51(t130);
                    if (!t131) {
                        goto gm_lb18;
                    }
                    {
                        void * t132 = gm_l0;
                        void * t133 = gm_l0;
                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)t133) + 24));
                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                        if (o136) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 100);
                        uint64_t t137 = r135;
                        GM_ST(uint64_t, (((uint8_t *)t132) + 24), t137);
                    }
gm_lb20:;
                }
            }
gm_lb18:;
            void * t138 = gm_l0;
            uint64_t t139 = gm_l1;
            void * t140 = gm_l0;
            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)t140) + 24));
            gm_b16_8 t142;
            GM_ST(void *, (uint8_t *)&t142, (void *)(gm_smit2phoauonm));
            GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(60));
            gm_b0_1 t143 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t138, t139, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ts06tb4mldv43 t144;
            memset(&t144, 0, sizeof t144);
            F.ret = t144;
            goto gm_fret;
        }
gm_lb15:;
        {
            uint8_t t145 = gm_l2;
            uint8_t t146 = ((t145 == ((uint8_t)117ULL)));
            if (!t146) {
                goto gm_lb21;
            }
            void * t147 = gm_l0;
            void * t148 = gm_l0;
            uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)t148) + 24));
            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
            if (o151) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 359, 13);
            uint64_t t152 = r150;
            GM_ST(uint64_t, (((uint8_t *)t147) + 24), t152);
            void * t153 = gm_l0;
            uint64_t t154 = gm_l1;
            gm_ts06tb4mldv43 t155 = gm_f_lex_unicode_escape_ns81oou((void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t155;
            goto gm_fret;
        }
gm_lb21:;
        {
            {
                {
                    uint8_t t156 = gm_l2;
                    uint8_t t157 = ((t156 == ((uint8_t)10ULL)));
                    if (!t157) {
                        goto gm_lb24;
                    }
                    goto gm_lb23;
                }
gm_lb24:;
                {
                    uint8_t t158 = gm_l2;
                    uint8_t t159 = ((t158 == ((uint8_t)13ULL)));
                    if (!t159) {
                        goto gm_lb25;
                    }
                    goto gm_lb23;
                }
gm_lb25:;
                goto gm_lb22;
            }
gm_lb23:;
            gm_ts06tb4mldv43 t160;
            memset(&t160, 0, sizeof t160);
            F.ret = t160;
            goto gm_fret;
        }
gm_lb22:;
        {
            void * t161 = gm_l0;
            gm_tb31k5l794t7p t162 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t161));
            gm_l7 = t162;
            void * t163 = gm_l0;
            void * t164 = gm_l0;
            uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 24));
            uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t r167; bool o168 = __builtin_add_overflow(t165, t166, &r167);
            if (o168) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 365, 13);
            uint64_t t169 = r167;
            GM_ST(uint64_t, (((uint8_t *)t163) + 24), t169);
            void * t170 = gm_l0;
            uint64_t t171 = gm_l1;
            void * t172 = gm_l0;
            uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)t172) + 24));
            void * t174 = gm_l0;
            void * t175 = GM_LD(void *, (((uint8_t *)t174) + 8));
            uint64_t t176 = GM_LD(uint64_t, ((((uint8_t *)t174) + 8) + 8));
            uint64_t t177 = gm_l1;
            uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
            if (o179) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 366, 86);
            uint64_t t180 = r178;
            void * t181 = gm_l0;
            uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)t181) + 24));
            uint64_t t183 = (uintptr_t)t180;
            uint64_t t184 = (uintptr_t)t182;
            if (t183 > t184 || t184 > (uintptr_t)t176) gm_trap_slice(t183, t184, t176, gm_srct4c97p4hek1p, 366, 79);
            uint64_t t185 = ((uintptr_t)t182) - (uintptr_t)t180;
            gm_b16_8 t186;
            GM_ST(void *, (uint8_t *)&t186, (void *)(((uint8_t *)t175 + (uintptr_t)t180 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(t185));
            gm_tnpifv20irnph t187;
            GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t186);
            gm_f_format_eue6enu((void *)&gm_l11, (void *)NULL, cx, NULL, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srct4c97p4hek1p, 366, 34);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
            gm_b0_1 t191 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t170, t171, t173, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ts06tb4mldv43 t192;
            memset(&t192, 0, sizeof t192);
            F.ret = t192;
            goto gm_fret;
        }
gm_lb26:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 345, 5);
    }
gm_lb14:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_field_index_pf905g6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tq7s98ihonkju gm_l3;
    {
        for (;;) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 24));
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
            uint8_t t11 = (t8 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 8));
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 8) + 8));
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 24));
            if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srct4c97p4hek1p, 154, 50);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t16 * 1));
            uint8_t t18 = gm_f_is_digit_2hoggnj(t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = gm_l0;
                void * t20 = gm_l0;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 24));
                uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 154, 68);
                uint64_t t24 = r22;
                GM_ST(uint64_t, (((uint8_t *)t19) + 24), t24);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 24));
            uint64_t t27 = gm_l1;
            uint64_t r28; bool o29 = __builtin_sub_overflow(t26, t27, &r28);
            if (o29) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 155, 8);
            uint64_t t30 = r28;
            uint8_t t31 = (t30 > ((uint64_t)1ULL));
            if (!t31) {
                goto gm_lb5;
            }
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
            uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 8) + 8));
            uint64_t t35 = gm_l1;
            if ((uintptr_t)t35 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t35, t34, gm_srct4c97p4hek1p, 155, 28);
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
            uint8_t t37 = ((t36 == ((uint8_t)48ULL)));
            if (!t37) {
                goto gm_lb5;
            }
            void * t38 = gm_l0;
            uint64_t t39 = gm_l1;
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 24));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sqj9769nr8pm0));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(47));
            gm_b0_1 t43 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t38, t39, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    {
        void * t44 = gm_l0;
        uint64_t t45 = gm_l1;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 24));
        gm_t2lqu8jsj511d t48;
        GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t45);
        GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
        gm_f_int_value_cnmrbr4((void *)&F.l2, (void *)NULL, cx, NULL, t44, t48, ((uint32_t)10ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        gm_t46lmulm8gaj3 t50;
        memset(&t50, 0, sizeof t50);
        gm_tbci5ospk5nnn t51 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
        gm_ta42iqot1auae t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 61, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 62, 0);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t52 + 0), t51);
        GM_ST(gm_t46lmulm8gaj3, ((uint8_t *)&t52 + 48), t50);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 60), ((uint8_t)0));
        gm_tq7s98ihonkju t53 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t52, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khsmm7kmq9o)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t53;
        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srct4c97p4hek1p, 159, 18);
        void * t55 = GM_LD(void *, (((uint8_t *)&gm_l3) + 8));
        uint64_t t56 = gm_l1;
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 24));
        gm_t3fcbuehh3bs2 t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t59 + 8), t55);
        gm_b0_1 t60 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t49, &t59, t56, t58);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_number_9j9sdfi F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* n */
    gm_th2lqbo77pvsc gm_l3; /* err */
    gm_th2lqbo77pvsc gm_l4; /* prefix_fix */
    uint32_t gm_l5; /* radix */
    uint8_t gm_l6; /* c */
    uint8_t gm_l7; /* lower */
    gm_b16_8 gm_l8; /* letter */
    gm_t2lqu8jsj511d gm_l9; /* int_part */
    gm_tc70a9dbbmgkg gm_l10; /* frac_part */
    gm_t913bb735pvt5 gm_l11; /* exp */
    uint8_t gm_l12; /* float_by_suffix */
    uint64_t gm_l13; /* at */
    uint8_t gm_l14; /* is_float */
    gm_b16_8 gm_l15; /* suffix */
    uint8_t gm_l16; /* imag */
    gm_t46lmulm8gaj3 gm_l17; /* int_sfx */
    gm_taari1s0m51a7 gm_l18; /* float_sfx */
    gm_b16_8 gm_l19; /* message */
    gm_tj2m9dbh1ekv6 gm_l20; /* s */
    gm_b16_8 gm_l21; /* message */
    gm_b16_8 gm_l23; /* p */
    GM_STACK_CHECK(&F, 3239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint32_t gm_l29;
    uint8_t gm_l30;
    gm_tabb710d9mq2e gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    uint8_t gm_l37;
    gm_taari1s0m51a7 gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_tabb710d9mq2e gm_l45;
    uint8_t gm_l46;
    uint8_t gm_l47;
    gm_tabb710d9mq2e gm_l48;
    gm_tij245bdr7emd gm_l49;
    uint8_t gm_l50;
    gm_tabb710d9mq2e gm_l51;
    int64_t gm_l52;
    gm_tq7s98ihonkju gm_l53;
    gm_tq7s98ihonkju gm_l54;
    {
        void * t79 = gm_l0;
        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 24));
        gm_l1 = t80;
        void * t81 = gm_l0;
        uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)t81) + 40) + 8));
        gm_l2 = t82;
        {
            {
                uint64_t t83 = gm_l2;
                uint8_t t84 = (t83 > ((uint64_t)0ULL));
                if (!t84) {
                    goto gm_lb2;
                }
                {
                    void * t85 = gm_l0;
                    void * t86 = GM_LD(void *, (((uint8_t *)t85) + 40));
                    uint64_t t87 = GM_LD(uint64_t, ((((uint8_t *)t85) + 40) + 8));
                    uint64_t t88 = gm_l2;
                    uint64_t r89; bool o90 = __builtin_sub_overflow(t88, ((uint64_t)1ULL), &r89);
                    if (o90) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 33, 34);
                    uint64_t t91 = r89;
                    if ((uintptr_t)t91 >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)t91, t87, gm_srct4c97p4hek1p, 33, 18);
                    gm_t9qkh0rsooc8d t92;
                    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)58ULL));
                    uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t86) + (uintptr_t)t91 * 48) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)8ULL));
                    if (t94) {
                        gm_t9qkh0rsooc8d t95 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t86) + (uintptr_t)t91 * 48) + 8));
                        t94 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t95), ((uint8_t *)&t92));
                    }
                    uint8_t t96 = (uint8_t)(t94);
                    if (!t96) {
                        goto gm_lb2;
                    }
                }
                void * t97 = gm_l0;
                uint64_t t98 = gm_l1;
                gm_b0_1 t99 = gm_f_lex_field_index_pf905g6((void *)NULL, cx, NULL, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_th2lqbo77pvsc t100;
        memset(&t100, 0, sizeof t100);
        gm_l3 = t100;
        memset(&gm_l4, 0, sizeof gm_l4);
        gm_l5 = ((uint32_t)10ULL);
        void * t101 = gm_l0;
        void * t102 = GM_LD(void *, (((uint8_t *)t101) + 8));
        uint64_t t103 = GM_LD(uint64_t, ((((uint8_t *)t101) + 8) + 8));
        uint64_t t104 = gm_l1;
        if ((uintptr_t)t104 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t104, t103, gm_srct4c97p4hek1p, 42, 8);
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t102) + (uintptr_t)t104 * 1));
        uint8_t t106 = ((t105 == ((uint8_t)48ULL)));
        if (t106) {
            {
                {
                    void * t107 = gm_l0;
                    gm_tqs5p4n2tq19v t108 = gm_f_peek_at_74p8lkq(t107, ((uint64_t)1ULL));
                    gm_l26 = t108;
                    uint8_t t109 = (uint8_t)((((uint8_t *)&gm_l26))[1] != 0);
                    if (!t109) {
                        goto gm_lb4;
                    }
                    uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)&gm_l26));
                    gm_l6 = t110;
                    uint8_t t111 = gm_l6;
                    gm_t4q2b5ibsrdik t112;
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)120ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 1), ((uint8_t)111ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 2), ((uint8_t)98ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 3), ((uint8_t)88ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 4), ((uint8_t)79ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t112 + 5), ((uint8_t)66ULL));
                    uint8_t t113 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 0, ((uint8_t *)&t111))) t113 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 1, ((uint8_t *)&t111))) t113 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 2, ((uint8_t *)&t111))) t113 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 3, ((uint8_t *)&t111))) t113 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 4, ((uint8_t *)&t111))) t113 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t112) + 5, ((uint8_t *)&t111))) t113 = 1;
                    uint8_t t114 = (uint8_t)(t113);
                    if (t114) {
                        uint8_t t115 = gm_l6;
                        uint8_t t116 = (uint8_t)(t115 | ((uint8_t)32ULL));
                        gm_l7 = t116;
                        uint8_t t117 = gm_l6;
                        uint8_t t118 = (t117 < ((uint8_t)97ULL));
                        if (t118) {
                            {
                                {
                                    uint8_t t119 = gm_l7;
                                    uint8_t t120 = ((t119 == ((uint8_t)120ULL)));
                                    if (!t120) {
                                        goto gm_lb6;
                                    }
                                    gm_b16_8 t121;
                                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_se4629k8oa1uo));
                                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(1));
                                    gm_l8 = t121;
                                    goto gm_lb5;
                                }
gm_lb6:;
                                {
                                    uint8_t t122 = gm_l7;
                                    uint8_t t123 = ((t122 == ((uint8_t)111ULL)));
                                    if (!t123) {
                                        goto gm_lb7;
                                    }
                                    gm_b16_8 t124;
                                    GM_ST(void *, (uint8_t *)&t124, (void *)(gm_se0f2jka437t4));
                                    GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(1));
                                    gm_l8 = t124;
                                    goto gm_lb5;
                                }
gm_lb7:;
                                {
                                    gm_b16_8 t125;
                                    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_s50pmptf3idds));
                                    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(1));
                                    gm_l8 = t125;
                                    goto gm_lb5;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 47, 31);
                            }
gm_lb5:;
                            {
                                gm_b16_8 t126 = gm_l8;
                                gm_tnpifv20irnph t127;
                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t126);
                                gm_f_format_g6sh9k2((void *)&gm_l27, (void *)NULL, cx, NULL, t127);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srct4c97p4hek1p, 52, 27);
                            {
                                gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                gm_th2lqbo77pvsc t131;
                                GM_ST(uint8_t, (uint8_t *)&t131 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t131 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t131 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t131), t130);
                                ((uint8_t *)&t131)[16] = 1;
                                gm_l3 = t131;
                            }
                            {
                                gm_b16_8 t132 = gm_l8;
                                gm_tnpifv20irnph t133;
                                GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t132);
                                gm_f_format_78j0ecp((void *)&gm_l28, (void *)NULL, cx, NULL, t133);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srct4c97p4hek1p, 53, 34);
                            {
                                gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                                GM_ST(uint8_t, (uint8_t *)&gm_l4 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l4 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l4 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&gm_l4), t136);
                                ((uint8_t *)&gm_l4)[16] = 1;
                            }
                        }
                        void * t137 = gm_l0;
                        void * t138 = gm_l0;
                        uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t138) + 24));
                        uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)2ULL), &r140);
                        if (o141) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 55, 17);
                        uint64_t t142 = r140;
                        GM_ST(uint64_t, (((uint8_t *)t137) + 24), t142);
                        {
                            {
                                uint8_t t143 = gm_l7;
                                uint8_t t144 = ((t143 == ((uint8_t)120ULL)));
                                if (!t144) {
                                    goto gm_lb10;
                                }
                                gm_l29 = ((uint32_t)16ULL);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                uint8_t t145 = gm_l7;
                                uint8_t t146 = ((t145 == ((uint8_t)111ULL)));
                                if (!t146) {
                                    goto gm_lb11;
                                }
                                gm_l29 = ((uint32_t)8ULL);
                                goto gm_lb9;
                            }
gm_lb11:;
                            {
                                gm_l29 = ((uint32_t)2ULL);
                                goto gm_lb9;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 56, 25);
                        }
gm_lb9:;
                        uint32_t t147 = gm_l29;
                        gm_l5 = t147;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
    {
        void * t148 = gm_l0;
        uint32_t t149 = gm_l5;
        void * t150 = (void *)((uint8_t *)&gm_l3);
        gm_t2lqu8jsj511d t151 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t148, t149, t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t151;
        {
            {
                uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t153 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                uint8_t t154 = ((t152 == t153));
                if (!t154) {
                    goto gm_lb14;
                }
                {
                    {
                        uint8_t t155 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                        if (t155) {
                            goto gm_lb16;
                        }
                        gm_l30 = ((uint8_t)1);
                        goto gm_lb15;
                    }
gm_lb16:;
                    {
                        gm_l30 = ((uint8_t)0);
                        goto gm_lb15;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 66, 39);
                }
gm_lb15:;
                uint8_t t156 = gm_l30;
                if (!t156) {
                    goto gm_lb14;
                }
                {
                    uint32_t t157 = gm_l5;
                    gm_b16_8 t158 = gm_f_radix_name_5kts8t0(t157);
                    void * t159 = gm_l0;
                    void * t160 = GM_LD(void *, (((uint8_t *)t159) + 8));
                    uint64_t t161 = GM_LD(uint64_t, ((((uint8_t *)t159) + 8) + 8));
                    uint64_t t162 = gm_l1;
                    void * t163 = gm_l0;
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t163) + 24));
                    uint64_t t165 = (uintptr_t)t162;
                    uint64_t t166 = (uintptr_t)t164;
                    if (t165 > t166 || t166 > (uintptr_t)t161) gm_trap_slice(t165, t166, t161, gm_srct4c97p4hek1p, 67, 78);
                    uint64_t t167 = ((uintptr_t)t164) - (uintptr_t)t162;
                    gm_b16_8 t168;
                    GM_ST(void *, (uint8_t *)&t168, (void *)(((uint8_t *)t160 + (uintptr_t)t162 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(t167));
                    gm_t6j6smj7qoctu t169;
                    GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t158);
                    GM_ST(gm_b16_8, ((uint8_t *)&t169 + 16), t168);
                    gm_f_format_b6aggpq((void *)&gm_l31, (void *)NULL, cx, NULL, &t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srct4c97p4hek1p, 67, 15);
                {
                    gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                    gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                    gm_th2lqbo77pvsc t173;
                    GM_ST(uint8_t, (uint8_t *)&t173 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t173 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t173 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t173), t172);
                    ((uint8_t *)&t173)[16] = 1;
                    gm_l3 = t173;
                }
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
        {
            {
                uint32_t t174 = gm_l5;
                uint8_t t175 = ((t174 == ((uint32_t)10ULL)));
                if (!t175) {
                    goto gm_lb19;
                }
                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                uint64_t r178; bool o179 = __builtin_sub_overflow(t176, t177, &r178);
                if (o179) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 69, 24);
                uint64_t t180 = r178;
                uint8_t t181 = (t180 > ((uint64_t)1ULL));
                if (!t181) {
                    goto gm_lb19;
                }
                void * t182 = gm_l0;
                void * t183 = GM_LD(void *, (((uint8_t *)t182) + 8));
                uint64_t t184 = GM_LD(uint64_t, ((((uint8_t *)t182) + 8) + 8));
                uint64_t t185 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                if ((uintptr_t)t185 >= (uintptr_t)(t184)) gm_trap_index((uintptr_t)t185, t184, gm_srct4c97p4hek1p, 69, 58);
                uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)t183) + (uintptr_t)t185 * 1));
                uint8_t t187 = ((t186 == ((uint8_t)48ULL)));
                if (!t187) {
                    goto gm_lb19;
                }
                {
                    {
                        uint8_t t188 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                        if (t188) {
                            goto gm_lb21;
                        }
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb20;
                    }
gm_lb22:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 69, 89);
                }
gm_lb20:;
                uint8_t t189 = gm_l32;
                if (!t189) {
                    goto gm_lb19;
                }
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_sfd839bbgsa7s));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(62));
                gm_th2lqbo77pvsc t191;
                GM_ST(uint8_t, (uint8_t *)&t191 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t191 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t191 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t191), t190);
                ((uint8_t *)&t191)[16] = 1;
                gm_l3 = t191;
                goto gm_lb18;
            }
gm_lb19:;
        }
gm_lb18:;
    }
    {
        memset(&gm_l10, 0, sizeof gm_l10);
        gm_t913bb735pvt5 t192;
        memset(&t192, 0, sizeof t192);
        gm_l11 = t192;
        {
            {
                uint32_t t193 = gm_l5;
                gm_tcc1rq4n1avb2 t194;
                GM_ST(uint32_t, ((uint8_t *)&t194 + 0), ((uint32_t)10ULL));
                GM_ST(uint32_t, ((uint8_t *)&t194 + 4), ((uint32_t)16ULL));
                uint8_t t195 = 0;
                if (gm_eq467d5cm1d5(((uint8_t *)&t194) + 0, ((uint8_t *)&t193))) t195 = 1;
                if (gm_eq467d5cm1d5(((uint8_t *)&t194) + 4, ((uint8_t *)&t193))) t195 = 1;
                uint8_t t196 = (uint8_t)(t195);
                if (!t196) {
                    goto gm_lb24;
                }
                void * t197 = gm_l0;
                gm_tqs5p4n2tq19v t198 = gm_f_peek_72bostn(t197);
                gm_tqs5p4n2tq19v t199;
                GM_ST(uint8_t, ((uint8_t *)&t199), ((uint8_t)46ULL));
                ((uint8_t *)&t199)[1] = 1;
                uint8_t t200 = (gm_eqb44cb3cf1n(((uint8_t *)&t198), ((uint8_t *)&t199)));
                if (!t200) {
                    goto gm_lb24;
                }
                void * t201 = gm_l0;
                uint32_t t202 = gm_l5;
                void * t203 = gm_l0;
                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)t203) + 24));
                uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                if (o206) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 76, 78);
                uint64_t t207 = r205;
                uint8_t t208 = gm_f_fraction_follows_ivbj04h(t201, t202, t207);
                if (!t208) {
                    goto gm_lb24;
                }
                void * t209 = gm_l0;
                void * t210 = gm_l0;
                uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)t210) + 24));
                uint64_t r212; bool o213 = __builtin_add_overflow(t211, ((uint64_t)1ULL), &r212);
                if (o213) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 77, 9);
                uint64_t t214 = r212;
                GM_ST(uint64_t, (((uint8_t *)t209) + 24), t214);
                void * t215 = gm_l0;
                uint32_t t216 = gm_l5;
                void * t217 = (void *)((uint8_t *)&gm_l3);
                gm_t2lqu8jsj511d t218 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t215, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&gm_l10 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l10 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l10 + 20, 0);
                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l10), t218);
                ((uint8_t *)&gm_l10)[16] = 1;
                goto gm_lb23;
            }
gm_lb24:;
        }
gm_lb23:;
        uint32_t t219 = gm_l5;
        uint8_t t220 = ((t219 == ((uint32_t)10ULL)));
        if (t220) {
            void * t221 = gm_l0;
            gm_b16_8 t222;
            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_su7ft5rc766n4));
            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(2));
            uint8_t t223 = gm_f_at_exponent_qp4713f(t221, t222);
            if (t223) {
                void * t224 = gm_l0;
                void * t225 = (void *)((uint8_t *)&gm_l3);
                int64_t t226 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t224, t225);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t913bb735pvt5 t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t227 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t227), t226);
                ((uint8_t *)&t227)[8] = 1;
                gm_l11 = t227;
            }
        } else {
            uint32_t t228 = gm_l5;
            uint8_t t229 = ((t228 == ((uint32_t)16ULL)));
            if (t229) {
                void * t230 = gm_l0;
                gm_b16_8 t231;
                GM_ST(void *, (uint8_t *)&t231, (void *)(gm_s15p9kmrsfogp));
                GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(1));
                uint8_t t232 = gm_f_at_exponent_qp4713f(t230, t231);
                if (t232) {
                    void * t233 = gm_l0;
                    void * t234 = (void *)((uint8_t *)&gm_l3);
                    int64_t t235 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t233, t234);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t913bb735pvt5 t236;
                    GM_ST(uint8_t, (uint8_t *)&t236 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t236 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t236 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t236), t235);
                    ((uint8_t *)&t236)[8] = 1;
                    gm_l11 = t236;
                } else {
                    {
                        {
                            {
                                {
                                    uint8_t t237 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                                    if (t237) {
                                        goto gm_lb28;
                                    }
                                    gm_l33 = ((uint8_t)0);
                                    goto gm_lb27;
                                }
gm_lb28:;
                                {
                                    gm_l33 = ((uint8_t)1);
                                    goto gm_lb27;
                                }
gm_lb29:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 19);
                            }
gm_lb27:;
                            uint8_t t238 = gm_l33;
                            if (!t238) {
                                goto gm_lb26;
                            }
                            {
                                {
                                    uint8_t t239 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                                    if (t239) {
                                        goto gm_lb31;
                                    }
                                    gm_l34 = ((uint8_t)1);
                                    goto gm_lb30;
                                }
gm_lb31:;
                                {
                                    gm_l34 = ((uint8_t)0);
                                    goto gm_lb30;
                                }
gm_lb32:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 45);
                            }
gm_lb30:;
                            uint8_t t240 = gm_l34;
                            if (!t240) {
                                goto gm_lb26;
                            }
                            gm_b16_8 t241;
                            GM_ST(void *, (uint8_t *)&t241, (void *)(gm_s5frmv5tebja4));
                            GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(49));
                            gm_th2lqbo77pvsc t242;
                            GM_ST(uint8_t, (uint8_t *)&t242 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t242 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t242 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t242), t241);
                            ((uint8_t *)&t242)[16] = 1;
                            gm_l3 = t242;
                            goto gm_lb25;
                        }
gm_lb26:;
                    }
gm_lb25:;
                }
            }
        }
    }
    {
        gm_l12 = ((uint8_t)0);
        {
            {
                uint32_t t243 = gm_l5;
                uint8_t t244 = ((t243 == ((uint32_t)10ULL)));
                if (!t244) {
                    goto gm_lb34;
                }
                {
                    {
                        uint8_t t245 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                        if (t245) {
                            goto gm_lb36;
                        }
                        gm_l35 = ((uint8_t)1);
                        goto gm_lb35;
                    }
gm_lb36:;
                    {
                        gm_l35 = ((uint8_t)0);
                        goto gm_lb35;
                    }
gm_lb37:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 24);
                }
gm_lb35:;
                uint8_t t246 = gm_l35;
                if (!t246) {
                    goto gm_lb34;
                }
                {
                    {
                        uint8_t t247 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                        if (t247) {
                            goto gm_lb39;
                        }
                        gm_l36 = ((uint8_t)1);
                        goto gm_lb38;
                    }
gm_lb39:;
                    {
                        gm_l36 = ((uint8_t)0);
                        goto gm_lb38;
                    }
gm_lb40:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 46);
                }
gm_lb38:;
                uint8_t t248 = gm_l36;
                if (!t248) {
                    goto gm_lb34;
                }
                void * t249 = gm_l0;
                uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)t249) + 24));
                gm_l13 = t250;
                void * t251 = gm_l0;
                gm_b16_8 t252 = gm_f_scan_ident_h1b2n4u(t251);
                gm_taari1s0m51a7 t253 = gm_f_float_suffix_lh8qbg0(t252);
                gm_l38 = t253;
                {
                    {
                        uint8_t t254 = (uint8_t)((((uint8_t *)&gm_l38))[1] != 0);
                        if (t254) {
                            goto gm_lb42;
                        }
                        gm_l37 = ((uint8_t)0);
                        goto gm_lb41;
                    }
gm_lb42:;
                    {
                        gm_l37 = ((uint8_t)1);
                        goto gm_lb41;
                    }
gm_lb43:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 93, 27);
                }
gm_lb41:;
                uint8_t t255 = gm_l37;
                gm_l12 = t255;
                void * t256 = gm_l0;
                uint64_t t257 = gm_l13;
                GM_ST(uint64_t, (((uint8_t *)t256) + 24), t257);
                goto gm_lb33;
            }
gm_lb34:;
        }
gm_lb33:;
        {
            {
                uint8_t t258 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                if (t258) {
                    goto gm_lb45;
                }
                gm_l41 = ((uint8_t)0);
                goto gm_lb44;
            }
gm_lb45:;
            {
                gm_l41 = ((uint8_t)1);
                goto gm_lb44;
            }
gm_lb46:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 17);
        }
gm_lb44:;
        uint8_t t259 = gm_l41;
        if (t259) {
            gm_l40 = ((uint8_t)1);
        } else {
            {
                {
                    uint8_t t260 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                    if (t260) {
                        goto gm_lb48;
                    }
                    gm_l42 = ((uint8_t)0);
                    goto gm_lb47;
                }
gm_lb48:;
                {
                    gm_l42 = ((uint8_t)1);
                    goto gm_lb47;
                }
gm_lb49:;
                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 42);
            }
gm_lb47:;
            uint8_t t261 = gm_l42;
            gm_l40 = t261;
        }
    }
    {
        uint8_t t262 = gm_l40;
        if (t262) {
            gm_l39 = ((uint8_t)1);
        } else {
            uint8_t t263 = gm_l12;
            gm_l39 = t263;
        }
        uint8_t t264 = gm_l39;
        gm_l14 = t264;
        void * t265 = gm_l0;
        gm_b16_8 t266 = gm_f_scan_ident_h1b2n4u(t265);
        gm_l15 = t266;
        gm_b16_8 t267 = gm_l15;
        uint8_t t268 = gm_f_is_imag_suffix_46gu23m(t267);
        gm_l16 = t268;
        uint8_t t269 = gm_l16;
        if (t269) {
            {
                {
                    uint32_t t270 = gm_l5;
                    uint8_t t271 = (!(t270 == ((uint32_t)10ULL)));
                    if (!t271) {
                        goto gm_lb51;
                    }
                    {
                        {
                            uint8_t t272 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                            if (t272) {
                                goto gm_lb53;
                            }
                            gm_l43 = ((uint8_t)1);
                            goto gm_lb52;
                        }
gm_lb53:;
                        {
                            gm_l43 = ((uint8_t)0);
                            goto gm_lb52;
                        }
gm_lb54:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 103, 32);
                    }
gm_lb52:;
                    uint8_t t273 = gm_l43;
                    if (!t273) {
                        goto gm_lb51;
                    }
                    gm_b16_8 t274;
                    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_sc80lo6tf645h));
                    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(30));
                    gm_th2lqbo77pvsc t275;
                    GM_ST(uint8_t, (uint8_t *)&t275 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t275 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t275 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t275), t274);
                    ((uint8_t *)&t275)[16] = 1;
                    gm_l3 = t275;
                    goto gm_lb50;
                }
gm_lb51:;
            }
gm_lb50:;
            gm_l14 = ((uint8_t)1);
        } else {
            {
                {
                    gm_b16_8 t276 = gm_l15;
                    gm_b16_8 t277;
                    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sikg061j3im7q));
                    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(1));
                    uint8_t t278 = gm_f_starts_with_6vvt01e(t276, t277);
                    if (!t278) {
                        goto gm_lb56;
                    }
                    void * t279 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t281 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t282 = (uintptr_t)t280;
                    if (t281 > t282 || t282 > (uintptr_t)t280) gm_trap_slice(t281, t282, t280, gm_srct4c97p4hek1p, 105, 54);
                    uint64_t t283 = ((uintptr_t)t280) - (uintptr_t)((uint64_t)1ULL);
                    gm_b16_8 t284;
                    GM_ST(void *, (uint8_t *)&t284, (void *)(((uint8_t *)t279 + (uintptr_t)((uint64_t)1ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(t283));
                    uint8_t t285 = gm_f_all_digits_e77c2k1(t284);
                    if (!t285) {
                        goto gm_lb56;
                    }
                    uint32_t t286 = gm_l5;
                    uint8_t t287 = ((t286 == ((uint32_t)10ULL)));
                    if (!t287) {
                        goto gm_lb56;
                    }
                    {
                        {
                            uint8_t t288 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                            if (t288) {
                                goto gm_lb58;
                            }
                            gm_l44 = ((uint8_t)1);
                            goto gm_lb57;
                        }
gm_lb58:;
                        {
                            gm_l44 = ((uint8_t)0);
                            goto gm_lb57;
                        }
gm_lb59:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 105, 87);
                    }
gm_lb57:;
                    uint8_t t289 = gm_l44;
                    if (!t289) {
                        goto gm_lb56;
                    }
                    {
                        gm_b16_8 t290 = gm_l15;
                        gm_tnpifv20irnph t291;
                        GM_ST(gm_b16_8, ((uint8_t *)&t291 + 0), t290);
                        gm_f_format_v5s6luj((void *)&gm_l45, (void *)NULL, cx, NULL, t291);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t292 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                    if (!t292) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srct4c97p4hek1p, 106, 15);
                    {
                        gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                        gm_th2lqbo77pvsc t295;
                        GM_ST(uint8_t, (uint8_t *)&t295 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t295 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t295 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t295), t294);
                        ((uint8_t *)&t295)[16] = 1;
                        gm_l3 = t295;
                    }
                    goto gm_lb55;
                }
gm_lb56:;
            }
gm_lb55:;
        }
    }
    {
        gm_t46lmulm8gaj3 t296;
        memset(&t296, 0, sizeof t296);
        gm_l17 = t296;
        gm_taari1s0m51a7 t297;
        memset(&t297, 0, sizeof t297);
        gm_l18 = t297;
        uint64_t t298 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
        uint8_t t299 = ((t298 == ((uint64_t)0ULL)));
        if (t299) {
        } else {
            uint8_t t300 = gm_l16;
            if (t300) {
                gm_b16_8 t301 = gm_l15;
                gm_taari1s0m51a7 t302 = gm_f_imag_width_9gtosab(t301);
                gm_l18 = t302;
            } else {
                uint8_t t303 = gm_l14;
                if (t303) {
                    gm_b16_8 t304 = gm_l15;
                    gm_taari1s0m51a7 t305 = gm_f_float_suffix_lh8qbg0(t304);
                    gm_l18 = t305;
                    {
                        {
                            {
                                {
                                    uint8_t t306 = (uint8_t)((((uint8_t *)&gm_l18))[1] != 0);
                                    if (t306) {
                                        goto gm_lb63;
                                    }
                                    gm_l46 = ((uint8_t)1);
                                    goto gm_lb62;
                                }
gm_lb63:;
                                {
                                    gm_l46 = ((uint8_t)0);
                                    goto gm_lb62;
                                }
gm_lb64:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 12);
                            }
gm_lb62:;
                            uint8_t t307 = gm_l46;
                            if (!t307) {
                                goto gm_lb61;
                            }
                            {
                                {
                                    uint8_t t308 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                                    if (t308) {
                                        goto gm_lb66;
                                    }
                                    gm_l47 = ((uint8_t)1);
                                    goto gm_lb65;
                                }
gm_lb66:;
                                {
                                    gm_l47 = ((uint8_t)0);
                                    goto gm_lb65;
                                }
gm_lb67:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 34);
                            }
gm_lb65:;
                            uint8_t t309 = gm_l47;
                            if (!t309) {
                                goto gm_lb61;
                            }
                            {
                                gm_b16_8 t310 = gm_l15;
                                gm_tnpifv20irnph t311;
                                GM_ST(gm_b16_8, ((uint8_t *)&t311 + 0), t310);
                                gm_f_format_convcvp((void *)&gm_l48, (void *)NULL, cx, NULL, t311);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t312) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srct4c97p4hek1p, 117, 19);
                            {
                                gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                                gm_th2lqbo77pvsc t315;
                                GM_ST(uint8_t, (uint8_t *)&t315 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t315 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t315 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t315), t314);
                                ((uint8_t *)&t315)[16] = 1;
                                gm_l3 = t315;
                            }
                            goto gm_lb60;
                        }
gm_lb61:;
                    }
gm_lb60:;
                } else {
                    gm_b16_8 t316 = gm_l15;
                    gm_f_int_suffix_j594l00((void *)&gm_l49, (void *)NULL, cx, NULL, t316);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (t317) {
                                goto gm_lb69;
                            }
                            gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                            gm_l19 = t318;
                            {
                                {
                                    uint8_t t319 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                                    if (t319) {
                                        goto gm_lb71;
                                    }
                                    gm_l50 = ((uint8_t)1);
                                    goto gm_lb70;
                                }
gm_lb71:;
                                {
                                    gm_l50 = ((uint8_t)0);
                                    goto gm_lb70;
                                }
gm_lb72:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 122, 20);
                            }
gm_lb70:;
                            uint8_t t320 = gm_l50;
                            if (t320) {
                                gm_b16_8 t321 = gm_l19;
                                gm_th2lqbo77pvsc t322;
                                GM_ST(uint8_t, (uint8_t *)&t322 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t322 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t322 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t322), t321);
                                ((uint8_t *)&t322)[16] = 1;
                                gm_l3 = t322;
                            }
                            goto gm_lb68;
                        }
gm_lb69:;
                        {
                            uint8_t t323 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (!t323) {
                                goto gm_lb73;
                            }
                            gm_tj2m9dbh1ekv6 t324 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)&gm_l49) + 8));
                            gm_l20 = t324;
                            gm_tj2m9dbh1ekv6 t325 = gm_l20;
                            gm_t46lmulm8gaj3 t326;
                            GM_ST(uint8_t, (uint8_t *)&t326 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t326 + 10, 0);
                            GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t326), t325);
                            ((uint8_t *)&t326)[8] = 1;
                            gm_l17 = t326;
                            goto gm_lb68;
                        }
gm_lb73:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 120, 9);
                    }
gm_lb68:;
                }
            }
        }
    }
    {
        {
            {
                uint8_t t327 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                if (!t327) {
                    goto gm_lb75;
                }
                gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_l21 = t328;
                {
                    void * t329 = gm_l0;
                    uint64_t t330 = gm_l1;
                    void * t331 = gm_l0;
                    uint64_t t332 = GM_LD(uint64_t, (((uint8_t *)t331) + 24));
                    gm_tl874t2atnlh3 t333 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t329, t330, t332));
                    gm_b16_8 t334 = gm_l21;
                    gm_t4gut2c9fj1kt t335;
                    gm_f_error_s6hn871((void *)&t335, (void *)NULL, cx, NULL, t333, t334);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l22 = t335;
                }
                {
                    {
                        uint8_t t336 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
                        if (!t336) {
                            goto gm_lb77;
                        }
                        gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_l23 = t337;
                        void * t338 = (void *)((uint8_t *)&F.l22);
                        gm_b16_8 t339 = gm_l23;
                        gm_tnpifv20irnph t340;
                        GM_ST(gm_b16_8, ((uint8_t *)&t340 + 0), t339);
                        gm_f_format_ooibcq2((void *)&gm_l51, (void *)NULL, cx, NULL, t340);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t341 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                        if (!t341) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srct4c97p4hek1p, 130, 40);
                        gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                        gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                        void * t344 = gm_l0;
                        uint64_t t345 = gm_l1;
                        uint64_t t346 = gm_l1;
                        uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)2ULL), &r347);
                        if (o348) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 130, 82);
                        uint64_t t349 = r347;
                        gm_tl874t2atnlh3 t350 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t344, t345, t349));
                        gm_b16_8 t351 = gm_l23;
                        gm_b0_1 t352 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t338, t343, t350, t351);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb76;
                    }
gm_lb77:;
                }
gm_lb76:;
                {
                    void * t353 = gm_l0;
                    void * t354 = GM_LD(void *, (((uint8_t *)t353) + 32));
                    gm_f_push_o4ckdr1(t354, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l22)));
                }
                goto gm_lb74;
            }
gm_lb75:;
        }
gm_lb74:;
        uint8_t t355 = gm_l14;
        if (t355) {
            {
                void * t356 = gm_l0;
                uint32_t t357 = gm_l5;
                gm_t2lqu8jsj511d t358 = gm_l9;
                uint8_t t359 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                gm_tc70a9dbbmgkg t360 = GM_LD(gm_tc70a9dbbmgkg, ((uint8_t *)&gm_l10));
                if (t359) {
                    int64_t t361 = GM_LD(int64_t, ((uint8_t *)&gm_l11));
                    gm_l52 = t361;
                } else {
                    gm_l52 = ((int64_t)0LL);
                }
                int64_t t362 = gm_l52;
                gm_f_float_value_ip18r0p((void *)&F.l24, (void *)NULL, cx, NULL, t356, t357, t358, &t360, t362);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t363 = gm_l0;
                gm_taari1s0m51a7 t364 = gm_l18;
                uint8_t t365 = gm_l16;
                gm_tngtuiuskn938 t366 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l24));
                gm_tghmb53v8qi1e t367;
                GM_ST(uint8_t, (uint8_t *)&t367 + 99, 0);
                GM_ST(uint32_t, (uint8_t *)&t367 + 100, 0);
                GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t367 + 0), t366);
                GM_ST(gm_taari1s0m51a7, ((uint8_t *)&t367 + 96), t364);
                GM_ST(uint8_t, ((uint8_t *)&t367 + 98), t365);
                gm_tq7s98ihonkju t368 = gm_f_new_j2spmhj((void *)NULL, cx, NULL, &t367, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knasprnpdnf)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l53 = t368;
                uint8_t t369 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                if (!t369) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srct4c97p4hek1p, 135, 24);
                void * t370 = GM_LD(void *, (((uint8_t *)&gm_l53) + 8));
                uint64_t t371 = gm_l1;
                void * t372 = gm_l0;
                uint64_t t373 = GM_LD(uint64_t, (((uint8_t *)t372) + 24));
                gm_t3fcbuehh3bs2 t374;
                GM_ST(uint8_t, (uint8_t *)&t374 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t374 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t374 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t374 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t374 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t374 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t374 + 8), t370);
                gm_b0_1 t375 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t363, &t374, t371, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        } else {
            {
                void * t376 = gm_l0;
                gm_t2lqu8jsj511d t377 = gm_l9;
                uint32_t t378 = gm_l5;
                gm_f_int_value_cnmrbr4((void *)&F.l25, (void *)NULL, cx, NULL, t376, t377, t378);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t379 = gm_l0;
                gm_t46lmulm8gaj3 t380 = gm_l17;
                gm_tbci5ospk5nnn t381 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l25));
                gm_ta42iqot1auae t382;
                GM_ST(uint8_t, (uint8_t *)&t382 + 61, 0);
                GM_ST(uint16_t, (uint8_t *)&t382 + 62, 0);
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t382 + 0), t381);
                GM_ST(gm_t46lmulm8gaj3, ((uint8_t *)&t382 + 48), t380);
                GM_ST(uint8_t, ((uint8_t *)&t382 + 60), ((uint8_t)0));
                gm_tq7s98ihonkju t383 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t382, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2kjji37v2m)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l54 = t383;
                uint8_t t384 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                if (!t384) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srct4c97p4hek1p, 138, 22);
                void * t385 = GM_LD(void *, (((uint8_t *)&gm_l54) + 8));
                uint64_t t386 = gm_l1;
                void * t387 = gm_l0;
                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)t387) + 24));
                gm_t3fcbuehh3bs2 t389;
                GM_ST(uint8_t, (uint8_t *)&t389 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t389 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t389 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t389 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t389 + 0), ((uint8_t)3ULL));
                GM_ST(void *, ((uint8_t *)&t389 + 8), t385);
                gm_b0_1 t390 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t379, &t389, t386, t388);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_raw_string_uf6bd4g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint8_t gm_l5; /* b */
    uint64_t gm_l6; /* run */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1040u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 24));
    gm_l1 = t13;
    void * t14 = gm_l0;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 8));
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 24));
    uint64_t t18 = gm_f_backtick_run_l95fb09(t15, t17);
    gm_l2 = t18;
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 24));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t21, t22, &r23);
    if (o24) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 473, 5);
    uint64_t t25 = r23;
    GM_ST(uint64_t, (((uint8_t *)t19) + 24), t25);
    void * t26 = gm_l0;
    gm_tqs5p4n2tq19v t27 = gm_f_peek_72bostn(t26);
    gm_tqs5p4n2tq19v t28;
    GM_ST(uint8_t, ((uint8_t *)&t28), ((uint8_t)10ULL));
    ((uint8_t *)&t28)[1] = 1;
    uint8_t t29 = (gm_eqb44cb3cf1n(((uint8_t *)&t27), ((uint8_t *)&t28)));
    if (t29) {
        void * t30 = gm_l0;
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
        uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
        if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 475, 9);
        uint64_t t35 = r33;
        GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
    } else {
        {
            {
                void * t36 = gm_l0;
                gm_tqs5p4n2tq19v t37 = gm_f_peek_72bostn(t36);
                gm_tqs5p4n2tq19v t38;
                GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)13ULL));
                ((uint8_t *)&t38)[1] = 1;
                uint8_t t39 = (gm_eqb44cb3cf1n(((uint8_t *)&t37), ((uint8_t *)&t38)));
                if (!t39) {
                    goto gm_lb2;
                }
                void * t40 = gm_l0;
                gm_tqs5p4n2tq19v t41 = gm_f_peek_at_74p8lkq(t40, ((uint64_t)1ULL));
                gm_tqs5p4n2tq19v t42;
                GM_ST(uint8_t, ((uint8_t *)&t42), ((uint8_t)10ULL));
                ((uint8_t *)&t42)[1] = 1;
                uint8_t t43 = (gm_eqb44cb3cf1n(((uint8_t *)&t41), ((uint8_t *)&t42)));
                if (!t43) {
                    goto gm_lb2;
                }
                void * t44 = gm_l0;
                void * t45 = gm_l0;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 24));
                uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)2ULL), &r47);
                if (o48) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 477, 9);
                uint64_t t49 = r47;
                GM_ST(uint64_t, (((uint8_t *)t44) + 24), t49);
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t50;
        gm_f_list_uvfu1k1((void *)&t50, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t50;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb3;
            }
            {
                void * t51 = gm_l0;
                gm_tqs5p4n2tq19v t52 = gm_f_peek_72bostn(t51);
                gm_l9 = t52;
                uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
                if (t53) {
                    uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
                    gm_l5 = t54;
                } else {
                    {
                        void * t55 = gm_l0;
                        uint64_t t56 = gm_l1;
                        uint64_t t57 = gm_l1;
                        uint64_t t58 = gm_l2;
                        uint64_t r59; bool o60 = __builtin_add_overflow(t57, t58, &r59);
                        if (o60) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 482, 41);
                        uint64_t t61 = r59;
                        gm_tl874t2atnlh3 t62 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t55, t56, t61));
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_srnee0q49ttns));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(31));
                        gm_t4gut2c9fj1kt t64;
                        gm_f_error_s6hn871((void *)&t64, (void *)NULL, cx, NULL, t62, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l4 = t64;
                    }
                    {
                        void * t65 = (void *)((uint8_t *)&F.l4);
                        uint64_t t66 = gm_l2;
                        gm_t4ce0jd3tdro4 t67;
                        GM_ST(uint64_t, ((uint8_t *)&t67 + 0), t66);
                        gm_f_format_ikusttp((void *)&gm_l10, (void *)NULL, cx, NULL, t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srct4c97p4hek1p, 483, 24);
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                        gm_f_add_note_46v502s(t65, t70);
                    }
                    {
                        void * t71 = gm_l0;
                        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 32));
                        gm_f_push_o4ckdr1(t72, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
                    }
                    goto gm_lb3;
                }
                uint8_t t73 = gm_l5;
                gm_l5 = t73;
                uint8_t t74 = gm_l5;
                uint8_t t75 = ((t74 == ((uint8_t)96ULL)));
                if (t75) {
                    void * t76 = gm_l0;
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t76) + 8));
                    void * t78 = gm_l0;
                    uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 24));
                    uint64_t t80 = gm_f_backtick_run_l95fb09(t77, t79);
                    gm_l6 = t80;
                    void * t81 = gm_l0;
                    void * t82 = gm_l0;
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)t82) + 24));
                    uint64_t t84 = gm_l6;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t83, t84, &r85);
                    if (o86) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 489, 13);
                    uint64_t t87 = r85;
                    GM_ST(uint64_t, (((uint8_t *)t81) + 24), t87);
                    uint64_t t88 = gm_l6;
                    uint64_t t89 = gm_l2;
                    uint8_t t90 = ((t88 == t89));
                    if (t90) {
                        goto gm_lb3;
                    }
                    gm_l8 = ((uint64_t)0ULL);
                    uint64_t t91 = gm_l6;
                    gm_l7 = t91;
                    {
                        for (;;) {
                            uint64_t t92 = gm_l8;
                            uint64_t t93 = gm_l7;
                            uint8_t t94 = (t92 < t93);
                            if (!t94) {
                                goto gm_lb6;
                            }
                            {
                                {
                                    {
                                        void * t95 = (void *)((uint8_t *)&F.l3);
                                        gm_tftb1odc739t5 t96 = gm_f_push_t83rg2m(t95, ((uint8_t)96ULL));
                                        gm_l11 = t96;
                                        uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                        if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srct4c97p4hek1p, 491, 31);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 491, 13);
                                }
gm_lb9:;
                            }
gm_lb8:;
                            uint64_t t98 = gm_l8;
                            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                            uint64_t t101 = r99;
                            gm_l8 = t101;
                        }
                    }
gm_lb6:;
                } else {
                    uint8_t t102 = gm_l5;
                    uint8_t t103 = ((t102 == ((uint8_t)13ULL)));
                    if (t103) {
                        void * t104 = gm_l0;
                        gm_tqs5p4n2tq19v t105 = gm_f_peek_at_74p8lkq(t104, ((uint64_t)1ULL));
                        gm_tqs5p4n2tq19v t106;
                        GM_ST(uint8_t, ((uint8_t *)&t106), ((uint8_t)10ULL));
                        ((uint8_t *)&t106)[1] = 1;
                        uint8_t t107 = (gm_eqb44cb3cf1n(((uint8_t *)&t105), ((uint8_t *)&t106)));
                        if (t107) {
                            void * t108 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t109 = gm_f_push_t83rg2m(t108, ((uint8_t)10ULL));
                            gm_l12 = t109;
                            uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srct4c97p4hek1p, 494, 17);
                            void * t111 = gm_l0;
                            void * t112 = gm_l0;
                            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t112) + 24));
                            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)2ULL), &r114);
                            if (o115) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 495, 17);
                            uint64_t t116 = r114;
                            GM_ST(uint64_t, (((uint8_t *)t111) + 24), t116);
                        } else {
                            void * t117 = gm_l0;
                            gm_b0_1 t118 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t117);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    } else {
                        void * t119 = (void *)((uint8_t *)&F.l3);
                        uint8_t t120 = gm_l5;
                        gm_tftb1odc739t5 t121 = gm_f_push_t83rg2m(t119, t120);
                        gm_l13 = t121;
                        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srct4c97p4hek1p, 500, 13);
                        void * t123 = gm_l0;
                        void * t124 = gm_l0;
                        uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 24));
                        uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                        if (o127) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 501, 13);
                        uint64_t t128 = r126;
                        GM_ST(uint64_t, (((uint8_t *)t123) + 24), t128);
                    }
                }
            }
gm_lb5:;
        }
    }
gm_lb3:;
    {
        void * t129 = gm_l0;
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_t9qkh0rsooc8d t131;
        GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)1ULL));
        gm_tibupt4dmcqai t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t132 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t132 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t130);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t132 + 16), t131);
        uint64_t t133 = gm_l1;
        void * t134 = gm_l0;
        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)t134) + 24));
        gm_t3fcbuehh3bs2 t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t136 + 8), t132);
        gm_b0_1 t137 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t129, &t136, t133, t135);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_lex_string_bcdbdjm F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* c */
    uint8_t gm_l6; /* x */
    GM_STACK_CHECK(&F, 542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l7;
    gm_ts06tb4mldv43 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t11 = gm_l0;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 24));
    uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 425, 5);
    uint64_t t16 = r14;
    GM_ST(uint64_t, (((uint8_t *)t11) + 24), t16);
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_uvfu1k1((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t17;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t18 = gm_l0;
                gm_tqs5p4n2tq19v t19 = gm_f_peek_72bostn(t18);
                gm_l7 = t19;
                uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                if (t20) {
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    gm_l4 = t21;
                } else {
                    void * t22 = gm_l0;
                    uint64_t t23 = gm_l1;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sjblck9bpb6r8));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(27));
                    gm_b0_1 t27 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t22, t23, t25, t26);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
                uint8_t t28 = gm_l4;
                uint8_t t29 = ((t28 == ((uint8_t)34ULL)));
                if (t29) {
                    void * t30 = gm_l0;
                    void * t31 = gm_l0;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 433, 13);
                    uint64_t t35 = r33;
                    GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
                    goto gm_lb1;
                }
                uint8_t t36 = gm_l4;
                uint8_t t37 = ((t36 == ((uint8_t)10ULL)));
                if (t37) {
                    void * t38 = gm_l0;
                    uint64_t t39 = gm_l1;
                    gm_b0_1 t40 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t38, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
                uint8_t t41 = gm_l4;
                uint8_t t42 = ((t41 == ((uint8_t)13ULL)));
                if (t42) {
                    void * t43 = gm_l0;
                    gm_tqs5p4n2tq19v t44 = gm_f_peek_at_74p8lkq(t43, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t45;
                    GM_ST(uint8_t, ((uint8_t *)&t45), ((uint8_t)10ULL));
                    ((uint8_t *)&t45)[1] = 1;
                    uint8_t t46 = (gm_eqb44cb3cf1n(((uint8_t *)&t44), ((uint8_t *)&t45)));
                    if (t46) {
                        void * t47 = gm_l0;
                        uint64_t t48 = gm_l1;
                        gm_b0_1 t49 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t47, t48);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb1;
                    }
                    void * t50 = gm_l0;
                    gm_b0_1 t51 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                } else {
                    uint8_t t52 = gm_l4;
                    uint8_t t53 = ((t52 == ((uint8_t)92ULL)));
                    if (t53) {
                        void * t54 = gm_l0;
                        gm_ts06tb4mldv43 t55 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t55;
                        {
                            {
                                uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                if (!t56) {
                                    goto gm_lb5;
                                }
                                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                                if (!t58) {
                                    goto gm_lb5;
                                }
                                uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
                                gm_l5 = t59;
                                void * t60 = (void *)((uint8_t *)&F.l3);
                                uint32_t t61 = gm_l5;
                                gm_f_push_utf8_bjq6ubs(t60, t61);
                                goto gm_lb4;
                            }
gm_lb5:;
                            {
                                uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                if (!t62) {
                                    goto gm_lb6;
                                }
                                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)1ULL));
                                if (!t64) {
                                    goto gm_lb6;
                                }
                                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 4));
                                gm_l6 = t65;
                                void * t66 = (void *)((uint8_t *)&F.l3);
                                uint8_t t67 = gm_l6;
                                gm_tftb1odc739t5 t68 = gm_f_push_t83rg2m(t66, t67);
                                gm_l9 = t68;
                                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srct4c97p4hek1p, 449, 29);
                                goto gm_lb4;
                            }
gm_lb6:;
                            {
                                uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                if (t70) {
                                    goto gm_lb7;
                                }
                                goto gm_lb4;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 447, 13);
                        }
gm_lb4:;
                    } else {
                        void * t71 = (void *)((uint8_t *)&F.l3);
                        uint8_t t72 = gm_l4;
                        gm_tftb1odc739t5 t73 = gm_f_push_t83rg2m(t71, t72);
                        gm_l10 = t73;
                        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srct4c97p4hek1p, 453, 13);
                        void * t75 = gm_l0;
                        void * t76 = gm_l0;
                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)t76) + 24));
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        if (o79) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 454, 13);
                        uint64_t t80 = r78;
                        GM_ST(uint64_t, (((uint8_t *)t75) + 24), t80);
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            gm_t9qkh0rsooc8d t81 = gm_l2;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t81) + 0));
            uint8_t t83 = (uint8_t)((t82 == ((uint8_t)2ULL)));
            if (!t83) {
                goto gm_lb9;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            uint8_t t85 = 0;
            uint8_t t86 = ((uint8_t)0ULL);
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t84) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t84))) + i * 1, ((uint8_t *)&t86))) { t85 = 1; break; }
            uint8_t t87 = (uint8_t)(t85);
            if (!t87) {
                goto gm_lb9;
            }
            void * t88 = gm_l0;
            uint64_t t89 = gm_l1;
            void * t90 = gm_l0;
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 24));
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sil0do0eri7f7));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(43));
            gm_t9qkh0rsooc8d t93;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)2ULL));
            gm_b0_1 t94 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t88, t89, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    {
        void * t95 = gm_l0;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_t9qkh0rsooc8d t97 = gm_l2;
        gm_tibupt4dmcqai t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t96);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 16), t97);
        uint64_t t99 = gm_l1;
        void * t100 = gm_l0;
        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t100) + 24));
        gm_t3fcbuehh3bs2 t102;
        GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t102 + 8), t98);
        gm_b0_1 t103 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t95, &t102, t99, t101);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_unicode_escape_ns81oou F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* digits */
    uint32_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    uint32_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* why */
    GM_STACK_CHECK(&F, 954u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    void * t10 = gm_l0;
    gm_tqs5p4n2tq19v t11 = gm_f_peek_72bostn(t10);
    gm_tqs5p4n2tq19v t12;
    GM_ST(uint8_t, ((uint8_t *)&t12), ((uint8_t)123ULL));
    ((uint8_t *)&t12)[1] = 1;
    uint8_t t13 = (!gm_eqb44cb3cf1n(((uint8_t *)&t11), ((uint8_t *)&t12)));
    if (t13) {
        void * t14 = gm_l0;
        uint64_t t15 = gm_l1;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 24));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sh6hkhojsmti9));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(47));
        gm_b0_1 t19 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t14, t15, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ts06tb4mldv43 t20;
        memset(&t20, 0, sizeof t20);
        F.ret = t20;
        goto gm_fret;
    }
    void * t21 = gm_l0;
    void * t22 = gm_l0;
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 24));
    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
    if (o25) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 378, 5);
    uint64_t t26 = r24;
    GM_ST(uint64_t, (((uint8_t *)t21) + 24), t26);
    void * t27 = gm_l0;
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
    gm_l2 = t28;
    {
        for (;;) {
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 24));
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t31) + 8) + 8));
            uint8_t t33 = (t30 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 8));
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t34) + 8) + 8));
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 24));
            if ((uintptr_t)t38 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t38, t36, gm_srct4c97p4hek1p, 380, 54);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)t38 * 1));
            uint8_t t40 = gm_f_is_hex_digit_9lads51(t39);
            if (!t40) {
                goto gm_lb1;
            }
            {
                void * t41 = gm_l0;
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 24));
                uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                if (o45) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 380, 72);
                uint64_t t46 = r44;
                GM_ST(uint64_t, (((uint8_t *)t41) + 24), t46);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t47 = gm_l0;
    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
    uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 8) + 8));
    uint64_t t50 = gm_l2;
    void * t51 = gm_l0;
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 24));
    uint64_t t53 = (uintptr_t)t50;
    uint64_t t54 = (uintptr_t)t52;
    if (t53 > t54 || t54 > (uintptr_t)t49) gm_trap_slice(t53, t54, t49, gm_srct4c97p4hek1p, 381, 15);
    uint64_t t55 = ((uintptr_t)t52) - (uintptr_t)t50;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t48 + (uintptr_t)t50 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
    gm_l3 = t56;
    void * t57 = gm_l0;
    gm_tqs5p4n2tq19v t58 = gm_f_peek_72bostn(t57);
    gm_tqs5p4n2tq19v t59;
    GM_ST(uint8_t, ((uint8_t *)&t59), ((uint8_t)125ULL));
    ((uint8_t *)&t59)[1] = 1;
    uint8_t t60 = (!gm_eqb44cb3cf1n(((uint8_t *)&t58), ((uint8_t *)&t59)));
    if (t60) {
        void * t61 = gm_l0;
        uint64_t t62 = gm_l1;
        void * t63 = gm_l0;
        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 24));
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s4hf5j4eu72mv));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(65));
        gm_b0_1 t66 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t61, t62, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ts06tb4mldv43 t67;
        memset(&t67, 0, sizeof t67);
        F.ret = t67;
        goto gm_fret;
    }
    void * t68 = gm_l0;
    void * t69 = gm_l0;
    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 24));
    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
    if (o72) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 386, 5);
    uint64_t t73 = r71;
    GM_ST(uint64_t, (((uint8_t *)t68) + 24), t73);
    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t75 = ((t74 == ((uint64_t)0ULL)));
    if (t75) {
        gm_l10 = ((uint8_t)1);
    } else {
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t77 = (t76 > ((uint64_t)6ULL));
        gm_l10 = t77;
    }
    uint8_t t78 = gm_l10;
    if (t78) {
        void * t79 = gm_l0;
        uint64_t t80 = gm_l1;
        void * t81 = gm_l0;
        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 24));
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sajhc6og5bv8v));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(58));
        gm_b0_1 t84 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t79, t80, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ts06tb4mldv43 t85;
        memset(&t85, 0, sizeof t85);
        F.ret = t85;
        goto gm_fret;
    }
    gm_l4 = ((uint32_t)0ULL);
    gm_b16_8 t86 = gm_l3;
    gm_l5 = t86;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t87 = gm_l6;
            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t89 = (t87 < t88);
            if (!t89) {
                goto gm_lb4;
            }
            {
                void * t90 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t92 = gm_l6;
                {
                    {
                        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t90) + (uintptr_t)t92 * 1));
                        gm_l7 = t93;
                        uint32_t t94 = gm_l4;
                        uint32_t r95; bool o96 = __builtin_mul_overflow(t94, ((uint32_t)16ULL), &r95);
                        if (o96) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t97 = r95;
                        uint8_t t98 = gm_l7;
                        uint32_t t99 = gm_f_digit_value_8ogbrqp(t98);
                        uint32_t r100; bool o101 = __builtin_add_overflow(t97, t99, &r100);
                        if (o101) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t102 = r100;
                        gm_l4 = t102;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 392, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t103 = gm_l6;
            uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
            uint64_t t106 = r104;
            gm_l6 = t106;
        }
    }
gm_lb4:;
    uint32_t t107 = gm_l4;
    uint8_t t108 = (t107 > ((uint32_t)1114111ULL));
    if (t108) {
        gm_l11 = ((uint8_t)1);
    } else {
        uint32_t t109 = gm_l4;
        gm_l8 = t109;
        uint32_t t110 = gm_l8;
        uint8_t t111 = (((uint32_t)55296ULL) <= t110);
        if (t111) {
            uint32_t t112 = gm_l8;
            uint8_t t113 = (t112 <= ((uint32_t)57343ULL));
            gm_l12 = t113;
        } else {
            gm_l12 = ((uint8_t)0);
        }
        uint8_t t114 = gm_l12;
        gm_l11 = t114;
    }
    uint8_t t115 = gm_l11;
    if (t115) {
        uint32_t t116 = gm_l4;
        uint8_t t117 = (t116 > ((uint32_t)1114111ULL));
        if (t117) {
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sovc43d45he5a));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(20));
            gm_l9 = t118;
        } else {
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s32vm0u5vgiu5));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(17));
            gm_l9 = t119;
        }
        {
            void * t120 = gm_l0;
            uint64_t t121 = gm_l1;
            void * t122 = gm_l0;
            uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)t122) + 24));
            gm_b16_8 t124 = gm_l3;
            gm_b16_8 t125 = gm_l9;
            gm_t6j6smj7qoctu t126;
            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t124);
            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 16), t125);
            gm_f_format_ek7crui((void *)&gm_l13, (void *)NULL, cx, NULL, &t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srct4c97p4hek1p, 395, 30);
            gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
            gm_b0_1 t130 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t120, t121, t123, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ts06tb4mldv43 t131;
        memset(&t131, 0, sizeof t131);
        F.ret = t131;
        goto gm_fret;
    }
    uint32_t t132 = gm_l4;
    gm_tum2strs1ckr2 t133;
    GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t133 + 4), t132);
    gm_ts06tb4mldv43 t134;
    GM_ST(uint8_t, (uint8_t *)&t134 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t134 + 10, 0);
    GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t134), t133);
    ((uint8_t *)&t134)[8] = 1;
    F.ret = t134;
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
inline __attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1) {
    struct gm_fr_parse_valid_9fjef3j F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l1; /* radix */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t9 = gm_l0;
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
                        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                        gm_l4 = t16;
                        {
                            {
                                uint8_t t17 = gm_l4;
                                uint8_t t18 = (!(t17 == ((uint8_t)95ULL)));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                uint8_t t19 = gm_l4;
                                uint32_t t20 = gm_f_digit_value_8ogbrqp(t19);
                                uint32_t t21 = gm_l1;
                                uint8_t t22 = (t20 >= t21);
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 230, 5);
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
    gm_b16_8 t27 = gm_l0;
    uint32_t t28 = gm_l1;
    gm_f_parse_fbta82r((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t27, t28);
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
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_utf8_bjq6ubs F;
    void * gm_l0; /* out */
    uint32_t gm_l1; /* c */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    uint32_t t2 = gm_l1;
    uint8_t t3 = (t2 < ((uint32_t)128ULL));
    if (t3) {
        void * t4 = gm_l0;
        uint32_t t5 = gm_l1;
        uint8_t t6 = (uint8_t)t5;
        gm_tftb1odc739t5 t7 = gm_f_push_t83rg2m(t4, t6);
        gm_l2 = t7;
        uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srct4c97p4hek1p, 404, 9);
    } else {
        uint32_t t9 = gm_l1;
        uint8_t t10 = (t9 < ((uint32_t)2048ULL));
        if (t10) {
            void * t11 = gm_l0;
            uint32_t t12 = gm_l1;
            unsigned __int128 n13 = (unsigned __int128)((uint64_t)6ULL);
            if (n13 >= 32) gm_trap_shift((uint64_t)n13, "u32", gm_srct4c97p4hek1p, 406, 27);
            uint32_t t14 = (uint32_t)(t12 >> (int)n13);
            uint32_t t15 = (uint32_t)(((uint32_t)192ULL) | t14);
            uint8_t t16 = (uint8_t)t15;
            gm_tftb1odc739t5 t17 = gm_f_push_t83rg2m(t11, t16);
            gm_l3 = t17;
            uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srct4c97p4hek1p, 406, 9);
            void * t19 = gm_l0;
            uint32_t t20 = gm_l1;
            uint32_t t21 = (uint32_t)(t20 & ((uint32_t)63ULL));
            uint32_t t22 = (uint32_t)(((uint32_t)128ULL) | t21);
            uint8_t t23 = (uint8_t)t22;
            gm_tftb1odc739t5 t24 = gm_f_push_t83rg2m(t19, t23);
            gm_l4 = t24;
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srct4c97p4hek1p, 407, 9);
        } else {
            uint32_t t26 = gm_l1;
            uint8_t t27 = (t26 < ((uint32_t)65536ULL));
            if (t27) {
                void * t28 = gm_l0;
                uint32_t t29 = gm_l1;
                unsigned __int128 n30 = (unsigned __int128)((uint64_t)12ULL);
                if (n30 >= 32) gm_trap_shift((uint64_t)n30, "u32", gm_srct4c97p4hek1p, 409, 27);
                uint32_t t31 = (uint32_t)(t29 >> (int)n30);
                uint32_t t32 = (uint32_t)(((uint32_t)224ULL) | t31);
                uint8_t t33 = (uint8_t)t32;
                gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t28, t33);
                gm_l5 = t34;
                uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srct4c97p4hek1p, 409, 9);
                void * t36 = gm_l0;
                uint32_t t37 = gm_l1;
                unsigned __int128 n38 = (unsigned __int128)((uint64_t)6ULL);
                if (n38 >= 32) gm_trap_shift((uint64_t)n38, "u32", gm_srct4c97p4hek1p, 410, 28);
                uint32_t t39 = (uint32_t)(t37 >> (int)n38);
                uint32_t t40 = (uint32_t)(t39 & ((uint32_t)63ULL));
                uint32_t t41 = (uint32_t)(((uint32_t)128ULL) | t40);
                uint8_t t42 = (uint8_t)t41;
                gm_tftb1odc739t5 t43 = gm_f_push_t83rg2m(t36, t42);
                gm_l6 = t43;
                uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srct4c97p4hek1p, 410, 9);
                void * t45 = gm_l0;
                uint32_t t46 = gm_l1;
                uint32_t t47 = (uint32_t)(t46 & ((uint32_t)63ULL));
                uint32_t t48 = (uint32_t)(((uint32_t)128ULL) | t47);
                uint8_t t49 = (uint8_t)t48;
                gm_tftb1odc739t5 t50 = gm_f_push_t83rg2m(t45, t49);
                gm_l7 = t50;
                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srct4c97p4hek1p, 411, 9);
            } else {
                void * t52 = gm_l0;
                uint32_t t53 = gm_l1;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)18ULL);
                if (n54 >= 32) gm_trap_shift((uint64_t)n54, "u32", gm_srct4c97p4hek1p, 413, 27);
                uint32_t t55 = (uint32_t)(t53 >> (int)n54);
                uint32_t t56 = (uint32_t)(((uint32_t)240ULL) | t55);
                uint8_t t57 = (uint8_t)t56;
                gm_tftb1odc739t5 t58 = gm_f_push_t83rg2m(t52, t57);
                gm_l8 = t58;
                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srct4c97p4hek1p, 413, 9);
                void * t60 = gm_l0;
                uint32_t t61 = gm_l1;
                unsigned __int128 n62 = (unsigned __int128)((uint64_t)12ULL);
                if (n62 >= 32) gm_trap_shift((uint64_t)n62, "u32", gm_srct4c97p4hek1p, 414, 28);
                uint32_t t63 = (uint32_t)(t61 >> (int)n62);
                uint32_t t64 = (uint32_t)(t63 & ((uint32_t)63ULL));
                uint32_t t65 = (uint32_t)(((uint32_t)128ULL) | t64);
                uint8_t t66 = (uint8_t)t65;
                gm_tftb1odc739t5 t67 = gm_f_push_t83rg2m(t60, t66);
                gm_l9 = t67;
                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srct4c97p4hek1p, 414, 9);
                void * t69 = gm_l0;
                uint32_t t70 = gm_l1;
                unsigned __int128 n71 = (unsigned __int128)((uint64_t)6ULL);
                if (n71 >= 32) gm_trap_shift((uint64_t)n71, "u32", gm_srct4c97p4hek1p, 415, 28);
                uint32_t t72 = (uint32_t)(t70 >> (int)n71);
                uint32_t t73 = (uint32_t)(t72 & ((uint32_t)63ULL));
                uint32_t t74 = (uint32_t)(((uint32_t)128ULL) | t73);
                uint8_t t75 = (uint8_t)t74;
                gm_tftb1odc739t5 t76 = gm_f_push_t83rg2m(t69, t75);
                gm_l10 = t76;
                uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srct4c97p4hek1p, 415, 9);
                void * t78 = gm_l0;
                uint32_t t79 = gm_l1;
                uint32_t t80 = (uint32_t)(t79 & ((uint32_t)63ULL));
                uint32_t t81 = (uint32_t)(((uint32_t)128ULL) | t80);
                uint8_t t82 = (uint8_t)t81;
                gm_tftb1odc739t5 t83 = gm_f_push_t83rg2m(t78, t82);
                gm_l11 = t83;
                uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srct4c97p4hek1p, 416, 9);
            }
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_radix_name_5kts8t0 F;
    uint32_t gm_l0; /* radix */
    GM_STACK_CHECK_SHALLOW(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint32_t t7 = gm_l0;
            uint8_t t8 = ((t7 == ((uint32_t)2ULL)));
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s8e72gdumihb1));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(6));
            gm_l1 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint32_t t10 = gm_l0;
            uint8_t t11 = ((t10 == ((uint32_t)8ULL)));
            if (!t11) {
                goto gm_lb3;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_su30pqbigkn5m));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint32_t t13 = gm_l0;
            uint8_t t14 = ((t13 == ((uint32_t)10ULL)));
            if (!t14) {
                goto gm_lb4;
            }
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sqhghq789nvm0));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_safnb563bt6ue));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(11));
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 507, 43);
    }
gm_lb1:;
    gm_b16_8 t17 = gm_l1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_scan_digits_jhpsv6j F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    void * gm_l2; /* err */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* ok */
    gm_b16_8 gm_l6; /* text */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 845u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 24));
    gm_l3 = t21;
    {
        for (;;) {
            void * t22 = gm_l0;
            gm_tqs5p4n2tq19v t23 = gm_f_peek_72bostn(t22);
            gm_l11 = t23;
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
            if (!t24) {
                goto gm_lb1;
            }
            uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
            gm_l4 = t25;
            {
                uint32_t t26 = gm_l1;
                uint8_t t27 = ((t26 == ((uint32_t)16ULL)));
                if (t27) {
                    uint8_t t28 = gm_l4;
                    uint8_t t29 = gm_f_is_hex_digit_9lads51(t28);
                    gm_l5 = t29;
                } else {
                    uint8_t t30 = gm_l4;
                    uint8_t t31 = gm_f_is_digit_2hoggnj(t30);
                    gm_l5 = t31;
                }
                uint8_t t32 = gm_l5;
                if (t32) {
                    gm_l12 = ((uint8_t)1);
                } else {
                    uint8_t t33 = gm_l4;
                    uint8_t t34 = ((t33 == ((uint8_t)95ULL)));
                    gm_l12 = t34;
                }
                uint8_t t35 = gm_l12;
                uint8_t t36 = (uint8_t)!t35;
                if (t36) {
                    goto gm_lb1;
                }
                void * t37 = gm_l0;
                void * t38 = gm_l0;
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 24));
                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                if (o41) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 170, 9);
                uint64_t t42 = r40;
                GM_ST(uint64_t, (((uint8_t *)t37) + 24), t42);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t43 = gm_l0;
    void * t44 = GM_LD(void *, (((uint8_t *)t43) + 8));
    uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t43) + 8) + 8));
    uint64_t t46 = gm_l3;
    void * t47 = gm_l0;
    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 24));
    uint64_t t49 = (uintptr_t)t46;
    uint64_t t50 = (uintptr_t)t48;
    if (t49 > t50 || t50 > (uintptr_t)t45) gm_trap_slice(t49, t50, t45, gm_srct4c97p4hek1p, 172, 13);
    uint64_t t51 = ((uintptr_t)t48) - (uintptr_t)t46;
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t44 + (uintptr_t)t46 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51));
    gm_l6 = t52;
    {
        {
            void * t53 = gm_l2;
            {
                {
                    uint8_t t54 = (uint8_t)((((uint8_t *)t53))[16] != 0);
                    if (t54) {
                        goto gm_lb7;
                    }
                    gm_l13 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 173, 8);
            }
gm_lb6:;
            uint8_t t55 = gm_l13;
            if (!t55) {
                goto gm_lb5;
            }
            gm_b16_8 t56 = gm_l6;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
            uint8_t t58 = gm_f_starts_with_6vvt01e(t56, t57);
            if (t58) {
                gm_l15 = ((uint8_t)1);
            } else {
                gm_b16_8 t59 = gm_l6;
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sfdbfrv46lihd));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
                uint8_t t61 = gm_f_ends_with_biriu5l(t59, t60);
                gm_l15 = t61;
            }
            uint8_t t62 = gm_l15;
            if (t62) {
                gm_l14 = ((uint8_t)1);
            } else {
                gm_b16_8 t63 = gm_l6;
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sebojpbfjfgaf));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
                uint8_t t65 = gm_f_contains_gkki4kq(t63, t64);
                gm_l14 = t65;
            }
            uint8_t t66 = gm_l14;
            if (!t66) {
                goto gm_lb5;
            }
            void * t67 = gm_l2;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sg550i4fp569j));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(54));
            gm_th2lqbo77pvsc t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t69), t68);
            ((uint8_t *)&t69)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t67), t69);
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t70 = gm_l2;
    {
        {
            uint8_t t71 = (uint8_t)((((uint8_t *)t70))[16] != 0);
            if (t71) {
                goto gm_lb10;
            }
            gm_l16 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb10:;
        {
            gm_l16 = ((uint8_t)0);
            goto gm_lb9;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 176, 8);
    }
gm_lb9:;
    uint8_t t72 = gm_l16;
    if (t72) {
        gm_b16_8 t73 = gm_l6;
        gm_l7 = t73;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t74 = gm_l8;
                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t76 = (t74 < t75);
                if (!t76) {
                    goto gm_lb12;
                }
                {
                    uint64_t t77 = gm_l8;
                    void * t78 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t80 = gm_l8;
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t78) + (uintptr_t)t80 * 1));
                    gm_tf5srh4mp4b3i t82;
                    GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t77);
                    GM_ST(uint8_t, ((uint8_t *)&t82 + 8), t81);
                    gm_l17 = t82;
                    {
                        {
                            uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                            gm_l9 = t83;
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 8));
                            gm_l10 = t84;
                            {
                                {
                                    uint8_t t85 = gm_l10;
                                    uint8_t t86 = (!(t85 == ((uint8_t)95ULL)));
                                    if (!t86) {
                                        goto gm_lb18;
                                    }
                                    uint8_t t87 = gm_l10;
                                    uint32_t t88 = gm_f_digit_value_8ogbrqp(t87);
                                    uint32_t t89 = gm_l1;
                                    uint8_t t90 = (t88 >= t89);
                                    if (!t90) {
                                        goto gm_lb18;
                                    }
                                    {
                                        void * t91 = gm_l2;
                                        void * t92 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t94 = gm_l9;
                                        uint64_t t95 = gm_l9;
                                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                        if (o97) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 179, 79);
                                        uint64_t t98 = r96;
                                        uint64_t t99 = (uintptr_t)t94;
                                        uint64_t t100 = (uintptr_t)t98;
                                        if (t99 > t100 || t100 > (uintptr_t)t93) gm_trap_slice(t99, t100, t93, gm_srct4c97p4hek1p, 179, 71);
                                        uint64_t t101 = ((uintptr_t)t98) - (uintptr_t)t94;
                                        gm_b16_8 t102;
                                        GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t92 + (uintptr_t)t94 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(t101));
                                        uint32_t t103 = gm_l1;
                                        gm_b16_8 t104 = gm_f_radix_name_5kts8t0(t103);
                                        gm_t6j6smj7qoctu t105;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t102);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 16), t104);
                                        gm_f_format_51h4888((void *)&gm_l18, (void *)NULL, cx, NULL, &t105);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                        if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srct4c97p4hek1p, 179, 24);
                                        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                                        gm_th2lqbo77pvsc t109;
                                        GM_ST(uint8_t, (uint8_t *)&t109 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t109 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t109 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t109), t108);
                                        ((uint8_t *)&t109)[16] = 1;
                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t91), t109);
                                    }
                                    goto gm_lb12;
                                }
gm_lb18:;
                            }
gm_lb17:;
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 177, 9);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t110 = gm_l8;
                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                uint64_t t113 = r111;
                gm_l8 = t113;
            }
        }
gm_lb12:;
    }
    uint64_t t114 = gm_l3;
    void * t115 = gm_l0;
    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 24));
    gm_t2lqu8jsj511d t117;
    GM_ST(uint64_t, ((uint8_t *)&t117 + 0), t114);
    GM_ST(uint64_t, ((uint8_t *)&t117 + 8), t116);
    F.ret = t117;
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
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_scan_exponent_20o8k8o F;
    void * gm_l0; /* lx */
    void * gm_l1; /* err */
    uint8_t gm_l2; /* negative */
    gm_t2lqu8jsj511d gm_l3; /* digits */
    int64_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 608u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    int64_t gm_l10;
    void * t10 = gm_l0;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 24));
    uint64_t r13; bool o14 = __builtin_add_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 199, 5);
    uint64_t t15 = r13;
    GM_ST(uint64_t, (((uint8_t *)t10) + 24), t15);
    gm_l2 = ((uint8_t)0);
    void * t16 = gm_l0;
    gm_tqs5p4n2tq19v t17 = gm_f_peek_72bostn(t16);
    gm_tqs5p4n2tq19v t18;
    GM_ST(uint8_t, ((uint8_t *)&t18), ((uint8_t)45ULL));
    ((uint8_t *)&t18)[1] = 1;
    uint8_t t19 = (gm_eqb44cb3cf1n(((uint8_t *)&t17), ((uint8_t *)&t18)));
    if (t19) {
        void * t20 = gm_l0;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 24));
        uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 202, 9);
        uint64_t t25 = r23;
        GM_ST(uint64_t, (((uint8_t *)t20) + 24), t25);
        gm_l2 = ((uint8_t)1);
    } else {
        void * t26 = gm_l0;
        gm_tqs5p4n2tq19v t27 = gm_f_peek_72bostn(t26);
        gm_tqs5p4n2tq19v t28;
        GM_ST(uint8_t, ((uint8_t *)&t28), ((uint8_t)43ULL));
        ((uint8_t *)&t28)[1] = 1;
        uint8_t t29 = (gm_eqb44cb3cf1n(((uint8_t *)&t27), ((uint8_t *)&t28)));
        if (t29) {
            void * t30 = gm_l0;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 205, 9);
            uint64_t t35 = r33;
            GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
        }
    }
    void * t36 = gm_l0;
    void * t37 = gm_l1;
    gm_t2lqu8jsj511d t38 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t36, ((uint32_t)10ULL), t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t38;
    gm_l4 = ((int64_t)0LL);
    void * t39 = gm_l0;
    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 8));
    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 8) + 8));
    uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t44 = (uintptr_t)t42;
    uint64_t t45 = (uintptr_t)t43;
    if (t44 > t45 || t45 > (uintptr_t)t41) gm_trap_slice(t44, t45, t41, gm_srct4c97p4hek1p, 209, 14);
    uint64_t t46 = ((uintptr_t)t43) - (uintptr_t)t42;
    gm_b16_8 t47;
    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t40 + (uintptr_t)t42 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46));
    gm_l5 = t47;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t48 = gm_l6;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t50 = (t48 < t49);
            if (!t50) {
                goto gm_lb1;
            }
            {
                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t53 = gm_l6;
                {
                    {
                        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t51) + (uintptr_t)t53 * 1));
                        gm_l7 = t54;
                        uint8_t t55 = gm_l7;
                        uint8_t t56 = (!(t55 == ((uint8_t)95ULL)));
                        if (t56) {
                            int64_t t57 = gm_l4;
                            int64_t r58; bool o59 = __builtin_mul_overflow(t57, ((int64_t)10LL), &r58);
                            int64_t t60 = o59 ? (((t57 < 0) == (((int64_t)10LL) < 0)) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r58;
                            uint8_t t61 = gm_l7;
                            uint8_t r62; bool o63 = __builtin_sub_overflow(t61, ((uint8_t)48ULL), &r62);
                            if (o63) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 211, 41);
                            uint8_t t64 = r62;
                            int64_t t65 = (int64_t)t64;
                            int64_t r66; bool o67 = __builtin_add_overflow(t60, t65, &r66);
                            int64_t t68 = o67 ? ((t65 > 0) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r66;
                            int64_t t69 = GM_LD(int64_t, ((uint8_t *)gm_cobksqr67t4));
                            int64_t r70; bool o71 = __builtin_add_overflow(t69, ((int64_t)1LL), &r70);
                            if (o71) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 211, 55);
                            int64_t t72 = r70;
                            int64_t t73 = (t68 < t72 ? t68 : t72);
                            gm_l4 = t73;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 209, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t74 = gm_l6;
            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
            uint64_t t77 = r75;
            gm_l6 = t77;
        }
    }
gm_lb1:;
    int64_t t78 = gm_l4;
    int64_t t79 = GM_LD(int64_t, ((uint8_t *)gm_cobksqr67t4));
    uint8_t t80 = (t78 > t79);
    if (t80) {
        void * t81 = gm_l1;
        {
            {
                uint8_t t82 = (uint8_t)((((uint8_t *)t81))[16] != 0);
                if (t82) {
                    goto gm_lb7;
                }
                gm_l8 = ((uint8_t)1);
                goto gm_lb6;
            }
gm_lb7:;
            {
                gm_l8 = ((uint8_t)0);
                goto gm_lb6;
            }
gm_lb8:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 215, 12);
        }
gm_lb6:;
        uint8_t t83 = gm_l8;
        if (t83) {
            void * t84 = gm_l1;
            int64_t t85 = GM_LD(int64_t, ((uint8_t *)gm_cobksqr67t4));
            gm_tm98ap4112hee t86;
            GM_ST(int64_t, ((uint8_t *)&t86 + 0), t85);
            gm_f_format_ufd866t((void *)&gm_l9, (void *)NULL, cx, NULL, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srct4c97p4hek1p, 216, 20);
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            gm_th2lqbo77pvsc t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t90), t89);
            ((uint8_t *)&t90)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t84), t90);
        }
        gm_l4 = ((int64_t)0LL);
    }
    uint8_t t91 = gm_l2;
    if (t91) {
        int64_t t92 = gm_l4;
        if (t92 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 220, 26);
        int64_t t93 = (int64_t)((uint64_t)0 - (uint64_t)t92);
        gm_l10 = t93;
    } else {
        int64_t t94 = gm_l4;
        gm_l10 = t94;
    }
    int64_t t95 = gm_l10;
    F.ret = t95;
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
inline __attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_to_closing_quote_qmb6ii3 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l3;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    gm_l1 = t6;
    {
        for (;;) {
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            gm_tqs5p4n2tq19v t9 = gm_f_byte_b4v8vul(t7, t8);
            gm_l3 = t9;
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
            if (!t10) {
                goto gm_lb1;
            }
            uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            gm_l2 = t11;
            {
                uint8_t t12 = gm_l2;
                uint8_t t13 = ((t12 == ((uint8_t)39ULL)));
                if (t13) {
                    void * t14 = gm_l0;
                    uint64_t t15 = gm_l1;
                    uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
                    if (o17) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 310, 22);
                    uint64_t t18 = r16;
                    GM_ST(uint64_t, (((uint8_t *)t14) + 24), t18);
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                uint8_t t19 = gm_l2;
                gm_tqs5p4n2tq19v t20;
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t20 + 1), ((uint8_t)13ULL));
                uint8_t t21 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t20) + 0, ((uint8_t *)&t19))) t21 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t20) + 1, ((uint8_t *)&t19))) t21 = 1;
                uint8_t t22 = (uint8_t)(t21);
                if (t22) {
                    goto gm_lb1;
                }
                uint64_t t23 = gm_l1;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 314, 9);
                uint64_t t26 = r24;
                gm_l1 = t26;
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_unterminated_at_line_end_v8hbf1f F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 300u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
        gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t2, t3, t5));
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sjblck9bpb6r8));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(27));
        gm_t4gut2c9fj1kt t8;
        gm_f_error_s6hn871((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t8;
    }
    void * t9 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sae23ckujbpp9));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(106));
    gm_f_add_note_46v502s(t9, t10);
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 32));
        gm_f_push_o4ckdr1(t12, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l2)));
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

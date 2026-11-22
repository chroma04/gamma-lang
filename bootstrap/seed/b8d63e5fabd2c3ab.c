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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csff7attl2ehc5 { uint64_t f0; uint32_t f1; } gm_csff7attl2ehc5;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; void * f2; } gm_tfim8ting9n8a;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csff7attl2ehc5) == 16 && _Alignof(gm_csff7attl2ehc5) == 8 && offsetof(gm_csff7attl2ehc5, f0) == 0 && offsetof(gm_csff7attl2ehc5, f1) == 8, "layout of `WithKey`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tn68el8d14vku f1; } gm_t4qfihdig0mro;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; } f1; } gm_t9rfq1ocel0om;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tfim8ting9n8a) == 24 && _Alignof(gm_tfim8ting9n8a) == 8 && offsetof(gm_tfim8ting9n8a, f0) == 0 && offsetof(gm_tfim8ting9n8a, f1) == 8 && offsetof(gm_tfim8ting9n8a, f2) == 16, "layout of `(fr: ^Frame, cx: u32, code: ^Code)`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; } f1; } gm_tl2g3fsjpi9op;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_tonn1ba3fjh5g;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4qfihdig0mro) == 24 && _Alignof(gm_t4qfihdig0mro) == 8 && offsetof(gm_t4qfihdig0mro, f0) == 0 && offsetof(gm_t4qfihdig0mro, f1) == 8, "layout of `(fiber: u32, key: WithKey)`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfim8ting9n8a v0; gm_tonn1ba3fjh5g v1; gm_t79rbdv4rafe5 v2; } f1; } gm_t8dl9beiqk70v;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tbl8il92l97ut v1; } f1; } gm_t9b5r56q9jk9c;
_Static_assert(sizeof(gm_t9rfq1ocel0om) == 12 && _Alignof(gm_t9rfq1ocel0om) == 4 && offsetof(gm_t9rfq1ocel0om, f0) == 0 && offsetof(gm_t9rfq1ocel0om, f1) == 4, "layout of `Arg`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl2g3fsjpi9op) == 24 && _Alignof(gm_tl2g3fsjpi9op) == 8 && offsetof(gm_tl2g3fsjpi9op, f0) == 0 && offsetof(gm_tl2g3fsjpi9op, f1) == 8, "layout of `FState`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t4qfihdig0mro v1; } f1; } gm_tmr8tdh5g7uj6;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tn68el8d14vku f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint32_t f3; uint8_t p4[4]; gm_trqfvmjtt4sij f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_to8g4620flbcm;
_Static_assert(sizeof(gm_tonn1ba3fjh5g) == 56 && _Alignof(gm_tonn1ba3fjh5g) == 8 && offsetof(gm_tonn1ba3fjh5g, f0) == 0 && offsetof(gm_tonn1ba3fjh5g, f1) == 16 && offsetof(gm_tonn1ba3fjh5g, f2) == 24 && offsetof(gm_tonn1ba3fjh5g, f3) == 40 && offsetof(gm_tonn1ba3fjh5g, f4) == 44, "layout of `(f: []u8, fty: Ty, arg: []u8, cx: u32, span: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; void * v1; } f1; } gm_tqheog0s00pcc;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8dl9beiqk70v) == 64 && _Alignof(gm_t8dl9beiqk70v) == 8 && offsetof(gm_t8dl9beiqk70v, f0) == 0 && offsetof(gm_t8dl9beiqk70v, f1) == 8, "layout of `Start`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9b5r56q9jk9c) == 32 && _Alignof(gm_t9b5r56q9jk9c) == 8 && offsetof(gm_t9b5r56q9jk9c, f0) == 0 && offsetof(gm_t9b5r56q9jk9c, f1) == 8, "layout of `Flow!(frame: ^Frame, label: u32)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t419mo45g7j0a v1; } f1; } gm_tcgnlqgmuvh2q;
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tmr8tdh5g7uj6) == 32 && _Alignof(gm_tmr8tdh5g7uj6) == 8 && offsetof(gm_tmr8tdh5g7uj6, f0) == 0 && offsetof(gm_tmr8tdh5g7uj6, f1) == 8, "layout of `Flow!(fiber: u32, key: WithKey)`");
_Static_assert(sizeof(gm_to8g4620flbcm) == 128 && _Alignof(gm_to8g4620flbcm) == 8 && offsetof(gm_to8g4620flbcm, f0) == 0 && offsetof(gm_to8g4620flbcm, f1) == 16 && offsetof(gm_to8g4620flbcm, f2) == 24 && offsetof(gm_to8g4620flbcm, f3) == 64 && offsetof(gm_to8g4620flbcm, f4) == 72 && offsetof(gm_to8g4620flbcm, f5) == 112, "layout of `WithRec`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqheog0s00pcc) == 32 && _Alignof(gm_tqheog0s00pcc) == 8 && offsetof(gm_tqheog0s00pcc, f0) == 0 && offsetof(gm_tqheog0s00pcc, f1) == 8, "layout of `Flow!^CallRec`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tcgnlqgmuvh2q) == 40 && _Alignof(gm_tcgnlqgmuvh2q) == 8 && offsetof(gm_tcgnlqgmuvh2q, f0) == 0 && offsetof(gm_tcgnlqgmuvh2q, f1) == 8, "layout of `GenSink`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_padgogk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_54qkesi {
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
struct gm_fr_fat_pnqciib {
    gm_frame h;
    void * ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_format_1amnh72 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2hd15t9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3pa9rer {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_gen_bytes_bvo3jme {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gen_emit_91g8h4i {
    gm_frame h;
    gm_tcgnlqgmuvh2q l3; /* sink */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_gen_fiber_nhmvpkp {
    gm_frame h;
    gm_t8dl9beiqk70v l13; /* start */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_gen_index_o6g9ar2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_gen_up_6h27fuu {
    gm_frame h;
    gm_tqheog0s00pcc ret;
};
struct gm_fr_gen_write_1pcgnkv {
    gm_frame h;
    gm_tcgnlqgmuvh2q l3; /* sink */
    gm_trqfvmjtt4sij l22; /* out */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_dst_b5cat1e {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_o51qq3k {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t6u2ggv {
    gm_frame h;
    gm_to8g4620flbcm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_cont_vgnbja2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j41e2v9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_key_a5oh9pl {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_slot_d_q5v1jqa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_up_get_n03r5d3 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_up_put_9i85d76 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vm_exit_518ppgp {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_vm_gen_qp72g0e {
    gm_frame h;
    gm_tdmi4fk9nhqa7 l6; /* fixed */
    gm_tcgnlqgmuvh2q l85; /* sink */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_vm_sub_uskpr78 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_vm_with_on_t2rnhma {
    gm_frame h;
    gm_to8g4620flbcm l12; /* w */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_abort_all_4him68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_cancel_fiber_n4k6buo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_check_stack_eei454t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_consume_u5eqint(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_drive_i7nr5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_54qkesi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fat_pnqciib(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_2hd15t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_3pa9rer(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cp3hiv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) uint32_t gm_f_frame_handle_k12gn88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_ggsg0cj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gen_bytes_bvo3jme(void * a0, gm_t9rfq1ocel0om a1);
__attribute__((aligned(4))) void gm_f_gen_emit_91g8h4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_gen_fiber_nhmvpkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7);
inline __attribute__((aligned(4))) uint64_t gm_f_gen_index_o6g9ar2(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_gen_up_6h27fuu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, void * a4);
__attribute__((aligned(4))) void gm_f_gen_write_1pcgnkv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6, uint8_t a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_label_dst_in_3iu4p2p(void *gm_out, void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_make_fill_f96auc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_fiber_39221jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tn68el8d14vku a1, const void * a2, const void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_perform_4kn6797(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, void * a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6u2ggv(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_put_cont_vgnbja2(void * a0, gm_b16_8 a1, uint32_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j41e2v9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) void gm_f_resolve_target_bdf5i1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_te9hrk0u35rev a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_same_key_a5oh9pl(gm_csff7attl2ehc5 a0, gm_csff7attl2ehc5 a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slot_d_q5v1jqa(void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_string_literal_337m6cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_swap_remove_7083s0t(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_up_get_n03r5d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, void * a5);
__attribute__((aligned(4))) void gm_f_up_put_9i85d76(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, void * a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vm_exit_518ppgp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_vm_gen_qp72g0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, void * a4);
GM_LONG_FN __attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_vm_sub_uskpr78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_vm_with_on_t2rnhma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3);
__attribute__((aligned(4))) void * gm_f_with_rec_2eah8tp(void * a0, gm_csff7attl2ehc5 a1);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_k031v9njpim[40] __attribute__((aligned(8)));
extern uint8_t gm_kdicmjcqqa3[40] __attribute__((aligned(8)));
extern uint8_t gm_kf4jqnuehk8[40] __attribute__((aligned(8)));
extern uint8_t gm_knjegli7s8j[40] __attribute__((aligned(8)));
extern uint8_t gm_kv68sf5221h[40] __attribute__((aligned(8)));
extern const uint8_t gm_s108thc59vc7u[37];
extern const uint8_t gm_s1kvbdtols1cc[53];
extern const uint8_t gm_s1vst5t3hdg5s[41];
extern const uint8_t gm_s2ef11ime8o01[42];
extern const uint8_t gm_s3mvfcnrk9f21[48];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5i05gtdgdefb[42];
extern const uint8_t gm_s64eu4ogr34e2[62];
extern const uint8_t gm_s76i2uidi0pjj[54];
extern const uint8_t gm_s7pgps43h1eg7[41];
extern const uint8_t gm_sar5aak6f8jpo[44];
extern const uint8_t gm_sco971hnpq7nk[49];
extern const uint8_t gm_sctncmc9osip3[47];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_si7q8ca92ce9b[41];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_sq8e4jok48g31[48];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srckqas99ada0fa[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss1mi142k161m[41];
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_tmpl gm_tpljbco1er3agg5;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplv05t19d54qoj;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_0fd5f52 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint32_t t5 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_padgogk F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)4ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)4ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_54qkesi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_54qkesi F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 128)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fat_pnqciib(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fat_pnqciib F;
    void * gm_l0; /* regs */
    uint64_t gm_l1; /* o */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = (void *)(((uint8_t *)t3) + (uintptr_t)t4 * 1);
    void * t6 = t5;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
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
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_addr_odbbf6a F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)gm_cmiriali67r));
    uint32_t t3 = gm_l1;
    uint64_t t4 = (uint64_t)t3;
    uint64_t r5; bool o6 = __builtin_mul_overflow(((uint64_t)16ULL), t4, &r5);
    if (o6) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 5, 64);
    uint64_t t7 = r5;
    uint64_t r8; bool o9 = __builtin_add_overflow(t2, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 5, 52);
    uint64_t t10 = r8;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_1amnh72 F;
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
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2hd15t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_2hd15t9 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s108thc59vc7u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv05t19d54qoj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3pa9rer(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pa9rer F;
    gm_tbccvikg6b96e gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5i05gtdgdefb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_cp3hiv2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljbco1er3agg5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_46el73v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnkfiltk5b43));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplav8bdvvt8rem), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_gen_index_o6g9ar2(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_gen_index_o6g9ar2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    unsigned __int128 gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l3;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l1;
    unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
    gm_l2 = t9;
    unsigned __int128 t10 = gm_l2;
    uint64_t t11 = (uint64_t)~((uint64_t)0ULL);
    unsigned __int128 t12 = (unsigned __int128)t11;
    uint8_t t13 = (t10 > t12);
    if (t13) {
        uint64_t t14 = (uint64_t)~((uint64_t)0ULL);
        gm_l3 = t14;
    } else {
        unsigned __int128 t15 = gm_l2;
        uint64_t t16 = (uint64_t)t15;
        gm_l3 = t16;
    }
    uint64_t t17 = gm_l3;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_label_dst_b5cat1e F;
    void * gm_l0; /* fr */
    uint32_t gm_l1; /* label */
    void * gm_l2; /* code */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 16));
    uint8_t t4 = (uint8_t)(GM_LD(void *, (((uint8_t *)t3) + 56)) != 0);
    if (t4) {
        void * t5 = GM_LD(void *, (((uint8_t *)t3) + 56));
        gm_l2 = t5;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t6 = gm_l0;
    void * t7 = gm_l2;
    uint32_t t8 = gm_l1;
    gm_f_label_dst_in_3iu4p2p((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t6, t7, t8);
    goto gm_fdone;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_addr_o51qq3k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint32_t t2 = gm_l3;
    uint64_t t3 = (uint64_t)t2;
    uint64_t t4 = gm_f_us_cuv01fp(t3);
    gm_l4 = t4;
    void * t5 = gm_l2;
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 64));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 64) + 8));
    uint64_t t8 = gm_l4;
    if ((uintptr_t)t8 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t8, t7, gm_src6hq50r4rk7e6, 191, 8);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t6) + (uintptr_t)t8 * 8));
    uint8_t t10 = (!(t9 == ((uint64_t)0ULL)));
    if (t10) {
        void * t11 = gm_l2;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 64));
        uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 64) + 8));
        uint64_t t14 = gm_l4;
        if ((uintptr_t)t14 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t14, t13, gm_src6hq50r4rk7e6, 191, 36);
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t15);
        goto gm_fdone;
    }
    void * t16 = gm_l0;
    gm_tl874t2atnlh3 t17 = gm_l1;
    void * t18 = gm_l2;
    uint32_t t19 = gm_l3;
    gm_f_local_alloc_qm9q4fo((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t16, t17, t18, t19);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 360) + 40));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 360) + 48));
    gm_t2lqu8jsj511d t6;
    GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2onqfca(t8, ((uint64_t)1ULL), t10);
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
    uint32_t t18 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6u2ggv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t6u2ggv F;
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
        gm_b16_8 t9 = gm_f_empty_slice_54qkesi();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j41e2v9(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 128), ((uint8_t *)a1), 128);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 128)));
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
inline __attribute__((aligned(4))) void gm_f_put_cont_vgnbja2(void * a0, gm_b16_8 a1, uint32_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_cont_vgnbja2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    uint32_t gm_l2; /* fiber */
    uint64_t gm_l3; /* epoch */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    uint32_t t4 = gm_l2;
    uint64_t t5 = (uint64_t)t4;
    gm_f_put_word_tfn443i(t2, t3, ((uint64_t)0ULL), t5);
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_l3;
    gm_f_put_word_tfn443i(t6, t7, ((uint64_t)1ULL), t8);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 106, 67);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t2, t11, t12);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_f_wr_word_npte9fs(t4, t5, t6);
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        uint64_t t9 = gm_l2;
        unsigned __int128 t10 = (unsigned __int128)t9;
        gm_f_put_uint_cddin99(t7, t8, t10);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j41e2v9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j41e2v9 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)128ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_key_a5oh9pl(gm_csff7attl2ehc5 a0, gm_csff7attl2ehc5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_key_a5oh9pl F;
    gm_tn68el8d14vku gm_l0; /* a */
    gm_tn68el8d14vku gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tn68el8d14vku, a0);
    gm_l1 = GM_CONV(gm_tn68el8d14vku, a1);
    uint8_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t7 = ((t5 == t6));
        gm_l2 = t7;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slot_d_q5v1jqa(void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_slot_d_q5v1jqa F;
    void * gm_l0; /* regs */
    uint32_t gm_l1; /* at */
    uint32_t gm_l2; /* d */
    uint32_t gm_l3; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint64_t t4 = (uint64_t)t3;
    void * t5 = gm_l0;
    uint32_t t6 = gm_l2;
    uint64_t t7 = (uint64_t)t6;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t5) + (uintptr_t)t7 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t r10; bool o11 = __builtin_add_overflow(t4, t9, &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 406, 77);
    uint64_t t12 = r10;
    void * t13 = gm_f_fat_pnqciib(t2, t12);
    uint32_t t14 = gm_l3;
    uint64_t t15 = (uint64_t)t14;
    uint64_t t16 = ((uintptr_t)t15) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
    F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_46el73v((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcpit8mdn0781q, 438, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t8);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_vm_sub_uskpr78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_vm_sub_uskpr78 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* code */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tbl8il92l97ut t3 = gm_l1;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 24));
    gm_f_vm_run_j1mlbuk((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4, t6);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 329, 13);
            uint64_t t8 = gm_l2;
            uint8_t t9 = (uint8_t)t8;
            GM_ST(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1), t9);
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 330, 13);
            uint64_t t12 = gm_l2;
            unsigned __int128 n13 = (unsigned __int128)((uint64_t)8ULL);
            if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_srcnqtpfma6qe4a, 330, 21);
            uint64_t t14 = (uint64_t)(t12 >> (int)n13);
            uint8_t t15 = (uint8_t)t14;
            GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1), t15);
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t17, gm_srcnqtpfma6qe4a, 331, 13);
            uint64_t t18 = gm_l2;
            unsigned __int128 n19 = (unsigned __int128)((uint64_t)16ULL);
            if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_srcnqtpfma6qe4a, 331, 21);
            uint64_t t20 = (uint64_t)(t18 >> (int)n19);
            uint8_t t21 = (uint8_t)t20;
            GM_ST(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)2ULL) * 1), t21);
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t23, gm_srcnqtpfma6qe4a, 332, 13);
            uint64_t t24 = gm_l2;
            unsigned __int128 n25 = (unsigned __int128)((uint64_t)24ULL);
            if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_srcnqtpfma6qe4a, 332, 21);
            uint64_t t26 = (uint64_t)(t24 >> (int)n25);
            uint8_t t27 = (uint8_t)t26;
            GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)((uint64_t)3ULL) * 1), t27);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t29, gm_srcnqtpfma6qe4a, 333, 13);
            uint64_t t30 = gm_l2;
            unsigned __int128 n31 = (unsigned __int128)((uint64_t)32ULL);
            if (n31 >= 64) gm_trap_shift((uint64_t)n31, "u64", gm_srcnqtpfma6qe4a, 333, 21);
            uint64_t t32 = (uint64_t)(t30 >> (int)n31);
            uint8_t t33 = (uint8_t)t32;
            GM_ST(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)4ULL) * 1), t33);
            void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t35, gm_srcnqtpfma6qe4a, 334, 13);
            uint64_t t36 = gm_l2;
            unsigned __int128 n37 = (unsigned __int128)((uint64_t)40ULL);
            if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_srcnqtpfma6qe4a, 334, 21);
            uint64_t t38 = (uint64_t)(t36 >> (int)n37);
            uint8_t t39 = (uint8_t)t38;
            GM_ST(uint8_t, (((uint8_t *)t34) + (uintptr_t)((uint64_t)5ULL) * 1), t39);
            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t41, gm_srcnqtpfma6qe4a, 335, 13);
            uint64_t t42 = gm_l2;
            unsigned __int128 n43 = (unsigned __int128)((uint64_t)48ULL);
            if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcnqtpfma6qe4a, 335, 21);
            uint64_t t44 = (uint64_t)(t42 >> (int)n43);
            uint8_t t45 = (uint8_t)t44;
            GM_ST(uint8_t, (((uint8_t *)t40) + (uintptr_t)((uint64_t)6ULL) * 1), t45);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t47, gm_srcnqtpfma6qe4a, 336, 13);
            uint64_t t48 = gm_l2;
            unsigned __int128 n49 = (unsigned __int128)((uint64_t)56ULL);
            if (n49 >= 64) gm_trap_shift((uint64_t)n49, "u64", gm_srcnqtpfma6qe4a, 336, 21);
            uint64_t t50 = (uint64_t)(t48 >> (int)n49);
            uint8_t t51 = (uint8_t)t50;
            GM_ST(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)7ULL) * 1), t51);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t53 = ((t52 == ((uint64_t)1ULL)));
        if (t53) {
            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t55, gm_srcnqtpfma6qe4a, 340, 13);
            uint64_t t56 = gm_l2;
            uint8_t t57 = (uint8_t)t56;
            GM_ST(uint8_t, (((uint8_t *)t54) + (uintptr_t)((uint64_t)0ULL) * 1), t57);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t59 = ((t58 == ((uint64_t)4ULL)));
        if (t59) {
            void * t60 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_srcnqtpfma6qe4a, 344, 13);
            uint64_t t62 = gm_l2;
            uint8_t t63 = (uint8_t)t62;
            GM_ST(uint8_t, (((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 1), t63);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t65, gm_srcnqtpfma6qe4a, 345, 13);
            uint64_t t66 = gm_l2;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)8ULL);
            if (n67 >= 64) gm_trap_shift((uint64_t)n67, "u64", gm_srcnqtpfma6qe4a, 345, 21);
            uint64_t t68 = (uint64_t)(t66 >> (int)n67);
            uint8_t t69 = (uint8_t)t68;
            GM_ST(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)1ULL) * 1), t69);
            void * t70 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t71, gm_srcnqtpfma6qe4a, 346, 13);
            uint64_t t72 = gm_l2;
            unsigned __int128 n73 = (unsigned __int128)((uint64_t)16ULL);
            if (n73 >= 64) gm_trap_shift((uint64_t)n73, "u64", gm_srcnqtpfma6qe4a, 346, 21);
            uint64_t t74 = (uint64_t)(t72 >> (int)n73);
            uint8_t t75 = (uint8_t)t74;
            GM_ST(uint8_t, (((uint8_t *)t70) + (uintptr_t)((uint64_t)2ULL) * 1), t75);
            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t77, gm_srcnqtpfma6qe4a, 347, 13);
            uint64_t t78 = gm_l2;
            unsigned __int128 n79 = (unsigned __int128)((uint64_t)24ULL);
            if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 347, 21);
            uint64_t t80 = (uint64_t)(t78 >> (int)n79);
            uint8_t t81 = (uint8_t)t80;
            GM_ST(uint8_t, (((uint8_t *)t76) + (uintptr_t)((uint64_t)3ULL) * 1), t81);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t82 = gm_l0;
    gm_b16_8 t83 = gm_l1;
    uint64_t t84 = gm_l2;
    unsigned __int128 t85 = (unsigned __int128)t84;
    gm_f_put_uint_cddin99(t82, t83, t85);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gen_bytes_bvo3jme(void * a0, gm_t9rfq1ocel0om a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_gen_bytes_bvo3jme F;
    void * gm_l0; /* regs */
    gm_t9rfq1ocel0om gm_l1; /* a */
    uint32_t gm_l2; /* at */
    uint32_t gm_l3; /* size */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l5;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)1ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l2 = t7;
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 4));
            gm_l3 = t8;
            void * t9 = gm_l0;
            uint32_t t10 = gm_l2;
            uint64_t t11 = (uint64_t)t10;
            void * t12 = gm_f_fat_pnqciib(t9, t11);
            uint32_t t13 = gm_l3;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = ((uintptr_t)t14) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15));
            gm_l5 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb3;
            }
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l4 = t19;
            void * t20 = gm_l0;
            uint32_t t21 = gm_l4;
            uint64_t t22 = (uint64_t)t21;
            uint64_t r23; bool o24 = __builtin_mul_overflow(t22, ((uint64_t)8ULL), &r23);
            if (o24) gm_trap("integer overflow in `*`", gm_srckqas99ada0fa, 90, 26);
            uint64_t t25 = r23;
            void * t26 = gm_f_fat_pnqciib(t20, t25);
            uint64_t t27 = ((uintptr_t)((uint64_t)8ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
            gm_l5 = t28;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 88, 50);
    }
gm_lb1:;
    gm_b16_8 t29 = gm_l5;
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_gen_emit_91g8h4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_gen_emit_91g8h4i F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l4; /* sv */
    gm_b16_8 gm_l5; /* data */
    uint64_t gm_l6; /* addr */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* sl */
    gm_tpmdr2hmdhsqr gm_l9; /* args */
    gm_t88rcucg1b7uk gm_l10; /* sty */
    gm_tiilmfd16v3fc gm_l11; /* effect_ */
    uint32_t gm_l12; /* op */
    gm_tcfa6l7reoosn gm_l13; /* r */
    GM_STACK_CHECK(&F, 787u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t833u93mr4iob gm_l14;
    {
        void * t5 = gm_l0;
        gm_tl874t2atnlh3 t6 = gm_l2;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t8 = (uint64_t)t7;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
        gm_f_alloc_mem_0vu6kcs((void *)&gm_l14, (void *)NULL, cx, NULL, t5, t6, t8, ((uint64_t)1ULL), t9, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (t10) {
    } else {
        gm_t76123gfet73g t11 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l14) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t11);
        goto gm_fdone;
    }
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
    gm_l6 = t12;
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 184);
        uint64_t t15 = gm_l6;
        gm_b16_8 t16 = gm_l5;
        gm_tgfdnufetdnfc t17;
        gm_f_init_9fuf99v((void *)&t17, t14, t15, t16);
    }
    void * t18 = gm_l0;
    gm_t2lqu8jsj511d t19 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t18));
    gm_l7 = t19;
    void * t20 = gm_l0;
    void * t21 = gm_l0;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 176));
    uint64_t r23; bool o24 = __builtin_mul_overflow(((uint64_t)2ULL), t22, &r23);
    if (o24) gm_trap("integer overflow in `*`", gm_srckqas99ada0fa, 309, 17);
    uint64_t t25 = r23;
    gm_b16_8 t26 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t20, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t26;
    void * t27 = gm_l0;
    gm_b16_8 t28 = gm_l8;
    uint64_t t29 = gm_l6;
    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t31 = (uint64_t)t30;
    gm_f_put_slice_ko14loi(t27, t28, t29, t31);
    gm_b16_8 t32 = gm_l8;
    gm_tpmdr2hmdhsqr t33;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
    gm_l9 = t33;
    {
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)0ULL));
            if (!t35) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a3) + 8));
            gm_l10 = t36;
            void * t37 = gm_l0;
            uint32_t t38 = gm_l1;
            gm_tl874t2atnlh3 t39 = gm_l2;
            gm_b16_8 t40 = gm_l4;
            gm_t88rcucg1b7uk t41 = gm_l10;
            void * t42 = (void *)((uint8_t *)&gm_l9);
            uint64_t t43 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t42 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_b16_8 t46 = gm_f_empty_slice_ijtp9lo();
            gm_f_call_value_fm9m0cc((void *)&gm_l13, (void *)NULL, cx, NULL, t37, t38, t39, t40, t41, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
            if (!t48) {
                goto gm_lb3;
            }
            {
                memmove(((uint8_t *)&gm_l11), (((uint8_t *)a3) + 8), 24);
            }
            uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)a3) + 8) + 24));
            gm_l12 = t49;
            {
                void * t50 = gm_l0;
                uint32_t t51 = gm_l1;
                gm_tl874t2atnlh3 t52 = gm_l2;
                void * t53 = (void *)((uint8_t *)&gm_l11);
                uint32_t t54 = gm_l12;
                void * t55 = (void *)((uint8_t *)&gm_l9);
                uint64_t t56 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t55 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                gm_b16_8 t59 = gm_f_empty_slice_ijtp9lo();
                gm_f_perform_4kn6797((void *)&gm_l13, (void *)NULL, cx, NULL, t50, t51, t52, t53, t54, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 312, 10);
    }
gm_lb1:;
    void * t60 = gm_l0;
    gm_t2lqu8jsj511d t61 = gm_l7;
    gm_f_release_jn5kqdi(t60, GM_CONV(gm_csc7lc6jlkqkuq, t61));
    void * t62 = gm_l0;
    void * t63 = (void *)(((uint8_t *)t62) + 184);
    uint64_t t64 = gm_l6;
    uint8_t t65 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t63, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l13), 32);
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
__attribute__((aligned(4))) void gm_f_gen_fiber_nhmvpkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_gen_fiber_nhmvpkp F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* fb */
    gm_t88rcucg1b7uk gm_l4; /* fty */
    gm_b16_8 gm_l5; /* ab */
    gm_b16_8 gm_l6; /* sb */
    gm_b16_8 gm_l7; /* dst */
    gm_tbl8il92l97ut gm_l8; /* inst */
    gm_tn68el8d14vku gm_l9; /* key */
    void * gm_l10; /* w */
    gm_b16_8 gm_l11; /* f */
    gm_b16_8 gm_l12; /* arg */
    uint32_t gm_l14; /* id */
    GM_STACK_CHECK(&F, 1143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_tcfa6l7reoosn gm_l15;
    void * gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * gm_l19;
    void * gm_l20;
    gm_tftb1odc739t5 gm_l21;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        gm_b16_8 t4 = gm_l6;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_f_check_stack_eei454t((void *)&gm_l15, (void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (t6) {
    } else {
        gm_t76123gfet73g t7 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l15) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t7);
        goto gm_fdone;
    }
    void * t8 = gm_l1;
    uint8_t t9 = (uint8_t)(((((uint8_t *)t8) + 176))[16] != 0);
    if (t9) {
        gm_tbl8il92l97ut t10 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)t8) + 176));
        gm_l8 = t10;
    } else {
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l2;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s2ef11ime8o01));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(41));
        gm_t76123gfet73g t14;
        gm_f_trap_qh84l9d((void *)&t14, (void *)NULL, cx, NULL, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t14);
        goto gm_fdone;
    }
    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l8));
    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)t15));
    uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 8));
    gm_tn68el8d14vku t18;
    GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t16);
    GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
    gm_l9 = t18;
    void * t19 = gm_l0;
    gm_tn68el8d14vku t20 = gm_l9;
    void * t21 = gm_f_with_rec_2eah8tp(t19, GM_CONV(gm_csff7attl2ehc5, t20));
    gm_l16 = t21;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l16)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l16));
        gm_l10 = t23;
    } else {
        void * t24 = gm_l0;
        gm_tl874t2atnlh3 t25 = gm_l2;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s64eu4ogr34e2));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(61));
        gm_t76123gfet73g t27;
        gm_f_trap_qh84l9d((void *)&t27, (void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t27);
        goto gm_fdone;
    }
    {
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_f_make_2ip78bj((void *)&gm_l17, (void *)NULL, cx, NULL, t28, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kv68sf5221h)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srckqas99ada0fa, 327, 10);
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_l11 = t30;
    gm_b16_8 t31 = gm_l11;
    gm_b16_8 t32 = gm_l3;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    gm_f_copy_from_f7j6p05(t31, t33);
    {
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t35 = (t34 > ((uint64_t)1ULL) ? t34 : ((uint64_t)1ULL));
        gm_f_make_2ip78bj((void *)&gm_l18, (void *)NULL, cx, NULL, t35, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k031v9njpim)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srckqas99ada0fa, 329, 12);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
    gm_l12 = t37;
    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l12));
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t42 = (uintptr_t)t40;
    if (t41 > t42 || t42 > (uintptr_t)t39) gm_trap_slice(t41, t42, t39, gm_srckqas99ada0fa, 330, 5);
    uint64_t t43 = ((uintptr_t)t40) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t38 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43));
    gm_b16_8 t45 = gm_l5;
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    gm_f_copy_from_f7j6p05(t44, t46);
    {
        gm_b16_8 t47 = gm_l11;
        gm_t88rcucg1b7uk t48 = gm_l4;
        void * t49 = GM_LD(void *, ((uint8_t *)&gm_l12));
        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t53 = (uintptr_t)t51;
        if (t52 > t53 || t53 > (uintptr_t)t50) gm_trap_slice(t52, t53, t50, gm_srckqas99ada0fa, 331, 33);
        uint64_t t54 = ((uintptr_t)t51) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
        void * t56 = gm_l10;
        uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)t56) + 64));
        void * t58 = gm_l10;
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 112));
        gm_tonn1ba3fjh5g t60;
        GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t47);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t60 + 16), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 24), t55);
        GM_ST(uint32_t, ((uint8_t *)&t60 + 40), t57);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t60 + 44), t59);
        GM_ST(uint8_t, (uint8_t *)&F.l13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l13 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&F.l13 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tonn1ba3fjh5g, ((uint8_t *)&F.l13 + 8), t60);
    }
    {
        void * t61 = gm_l0;
        gm_tn68el8d14vku t62 = gm_l9;
        gm_tn68el8d14vku t63 = gm_l9;
        gm_b16_8 t64 = gm_l6;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_tl2g3fsjpi9op t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t66 + 8), t63);
        uint32_t t67 = gm_f_new_fiber_39221jf((void *)NULL, cx, NULL, t61, t62, &(*(gm_t8dl9beiqk70v *)((uint8_t *)&F.l13)), &t66, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t67;
    }
    void * t68 = gm_l0;
    gm_tn68el8d14vku t69 = gm_l9;
    void * t70 = gm_f_with_rec_2eah8tp(t68, GM_CONV(gm_csff7attl2ehc5, t69));
    gm_l20 = t70;
    uint8_t t71 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l20)) != 0);
    if (t71) {
        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l20));
        gm_l19 = t72;
    } else {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
        goto gm_fdone;
    }
    void * t73 = gm_l19;
    void * t74 = (void *)(((uint8_t *)t73) + 24);
    uint32_t t75 = gm_l14;
    gm_tftb1odc739t5 t76 = gm_f_push_im9d4ro(t74, t75);
    gm_l21 = t76;
    uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srckqas99ada0fa, 333, 5);
    void * t78 = gm_l0;
    gm_b16_8 t79 = gm_l7;
    uint32_t t80 = gm_l14;
    gm_f_put_cont_vgnbja2(t78, t79, t80, ((uint64_t)1ULL));
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
extern __attribute__((aligned(4))) uint64_t gm_f_gen_index_o6g9ar2(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_gen_up_6h27fuu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, void * a4) {
    struct gm_fr_gen_up_6h27fuu F;
    void * gm_l0; /* m */
    void * gm_l1; /* code */
    void * gm_l2; /* regs */
    uint32_t gm_l3; /* levels */
    void * gm_l4; /* n */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * gm_l6;
    gm_tqheog0s00pcc gm_l7;
    void * gm_l8;
    void * t2 = gm_l2;
    void * t3 = gm_l1;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 88));
    uint64_t t5 = (uint64_t)t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t2) + (uintptr_t)t5 * 8));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    void * t8 = GM_INT_TO_PTR(t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
    if (t9) {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l6));
        gm_l5 = t10;
    } else {
        void * t11 = gm_l0;
        void * t12 = gm_l4;
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sco971hnpq7nk));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(48));
        gm_t76123gfet73g t15;
        gm_f_trap_qh84l9d((void *)&t15, (void *)NULL, cx, NULL, t11, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqheog0s00pcc *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqheog0s00pcc *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqheog0s00pcc *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tqheog0s00pcc *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tqheog0s00pcc *)gm_out) + 8), t15);
        goto gm_fdone;
    }
    void * t16 = gm_l5;
    uint32_t t17 = gm_l3;
    uint32_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint32_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srckqas99ada0fa, 411, 24);
    uint32_t t20 = r18;
    void * t21 = gm_f_frame_up_ghkkuqg(t16, t20);
    gm_l8 = t21;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l8));
        GM_ST(uint8_t, (uint8_t *)&gm_l7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l7 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l7 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l7 + 24, 0);
        ((uint8_t *)&gm_l7)[0] = 0;
        GM_ST(void *, ((uint8_t *)&gm_l7 + 8), t23);
    } else {
        void * t24 = gm_l0;
        void * t25 = gm_l4;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sco971hnpq7nk));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(48));
        gm_t76123gfet73g t28;
        gm_f_trap_qh84l9d((void *)&t28, (void *)NULL, cx, NULL, t24, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&gm_l7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l7 + 4, 0);
        ((uint8_t *)&gm_l7)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&gm_l7 + 8), t28);
    }
    memmove(gm_out, ((uint8_t *)&gm_l7), 32);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_gen_write_1pcgnkv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6, uint8_t a7, gm_b16_8 a8) {
    struct gm_fr_gen_write_1pcgnkv F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l4; /* pieces */
    gm_b16_8 gm_l5; /* specs */
    gm_b16_8 gm_l6; /* tys */
    uint8_t gm_l7; /* table */
    gm_b16_8 gm_l8; /* v */
    uint64_t gm_l9; /* first */
    gm_b16_8 gm_l10; /* sv */
    gm_b16_8 gm_l11; /* vals */
    gm_b16_8 gm_l12; /* ps */
    gm_b16_8 gm_l13; /* ss */
    gm_th2lqbo77pvsc gm_l14; /* index */
    uint64_t gm_l15; /* k */
    void * gm_l16; /* t */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    uint64_t gm_l20; /* j */
    gm_b16_8 gm_l21; /* ix */
    uint8_t gm_l23; /* custom */
    gm_tl572d7579hns gm_l24; /* r */
    gm_b16_8 gm_l25; /* msg */
    GM_STACK_CHECK(&F, 1743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tcfa6l7reoosn gm_l28;
    uint8_t gm_l29;
    gm_tcfa6l7reoosn gm_l30;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_l26 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l26 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 272, 23);
    }
gm_lb1:;
    uint8_t t15 = gm_l26;
    if (t15) {
        gm_l9 = ((uint64_t)1ULL);
    } else {
        gm_l9 = ((uint64_t)0ULL);
    }
    uint64_t t16 = gm_l9;
    uint8_t t17 = ((t16 == ((uint64_t)1ULL)));
    if (t17) {
        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l8));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_srckqas99ada0fa, 273, 32);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l10 = t20;
    } else {
        gm_b16_8 t21 = gm_f_empty_slice_ijtp9lo();
        gm_l10 = t21;
    }
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l8));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
    uint64_t t24 = gm_l9;
    uint64_t t25 = gm_l9;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    uint64_t r27; bool o28 = __builtin_add_overflow(t25, t26, &r27);
    if (o28) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 274, 22);
    uint64_t t29 = r27;
    uint64_t t30 = (uintptr_t)t24;
    uint64_t t31 = (uintptr_t)t29;
    if (t30 > t31 || t31 > (uintptr_t)t23) gm_trap_slice(t30, t31, t23, gm_srckqas99ada0fa, 274, 13);
    uint64_t t32 = ((uintptr_t)t29) - (uintptr_t)t24;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t22 + (uintptr_t)t24 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
    gm_l11 = t33;
    gm_b16_8 t34 = gm_l4;
    gm_l12 = t34;
    gm_b16_8 t35 = gm_l5;
    gm_l13 = t35;
    {
        memset(&gm_l14, 0, sizeof gm_l14);
    }
    uint8_t t36 = gm_l7;
    if (t36) {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, ((uint8_t *)&gm_l8));
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        uint64_t t40 = gm_l9;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 279, 26);
        uint64_t t44 = r42;
        if ((uintptr_t)t44 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t44, t39, gm_srckqas99ada0fa, 279, 24);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t38) + (uintptr_t)t44 * 16));
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        uint64_t t47 = gm_f_rd_word_8flcq6o(t37, t46);
        gm_l15 = t47;
        uint64_t t48 = gm_l15;
        uint8_t t49 = ((t48 == ((uint64_t)0ULL)));
        if (t49) {
            gm_l27 = ((uint8_t)1);
        } else {
            uint64_t t50 = gm_l15;
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t51) + 1328) + 8));
            uint64_t t53 = (uint64_t)t52;
            uint8_t t54 = (t50 > t53);
            gm_l27 = t54;
        }
        uint8_t t55 = gm_l27;
        if (t55) {
            void * t56 = gm_l0;
            gm_tl874t2atnlh3 t57 = gm_l2;
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sctncmc9osip3));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(46));
            gm_t76123gfet73g t59;
            gm_f_trap_qh84l9d((void *)&t59, (void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t59);
            goto gm_fdone;
        }
        void * t60 = gm_l0;
        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 1328));
        uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 1328) + 8));
        uint64_t t63 = gm_l15;
        uint64_t t64 = gm_f_us_cuv01fp(t63);
        uint64_t r65; bool o66 = __builtin_sub_overflow(t64, ((uint64_t)1ULL), &r65);
        if (o66) gm_trap("integer overflow in `-`", gm_srckqas99ada0fa, 281, 32);
        uint64_t t67 = r65;
        if ((uintptr_t)t67 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t67, t62, gm_srckqas99ada0fa, 281, 14);
        void * t68 = GM_LD(void *, (((uint8_t *)t61) + (uintptr_t)t67 * 8));
        gm_l16 = t68;
        void * t69 = gm_l16;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)t69));
        gm_l12 = t70;
        void * t71 = gm_l16;
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)t71) + 16));
        gm_l13 = t72;
        void * t73 = gm_l16;
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t73) + 32));
        GM_ST(uint8_t, (uint8_t *)&gm_l14 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l14 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l14 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l14), t74);
        ((uint8_t *)&gm_l14)[16] = 1;
    }
    gm_l18 = ((uint64_t)0ULL);
    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
    gm_l17 = t75;
    {
        for (;;) {
            uint64_t t77 = gm_l18;
            uint64_t t78 = gm_l17;
            uint8_t t79 = (t77 < t78);
            if (!t79) {
                goto gm_lb4;
            }
            {
                {
                    {
                        uint64_t t82 = gm_l18;
                        gm_l19 = t82;
                        {
                            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l12));
                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint64_t t86 = gm_l19;
                            uint64_t t87 = GM_LD(uint64_t, ((((uint8_t *)t84) + (uintptr_t)t86 * 16) + 8));
                            uint8_t t88 = (t87 > ((uint64_t)0ULL));
                            if (t88) {
                                {
                                    void * t89 = gm_l0;
                                    uint32_t t90 = gm_l1;
                                    gm_tl874t2atnlh3 t91 = gm_l2;
                                    gm_b16_8 t92 = gm_l10;
                                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                                    void * t94 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t96 = gm_l19;
                                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)t94) + (uintptr_t)t96 * 16));
                                    gm_tcgnlqgmuvh2q t98 = GM_LD(gm_tcgnlqgmuvh2q, ((uint8_t *)a3));
                                    gm_f_gen_emit_91g8h4i((void *)&gm_l28, (void *)NULL, cx, NULL, t89, t90, t91, &t98, t93, t97);
                                    if (gm_unw.kind) {
                                        goto gm_ll81;
                                    }
                                }
                                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (t99) {
                                } else {
                                    gm_t76123gfet73g t100 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l28) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t100);
                                    goto gm_fdone;
                                }
                            }
                            uint64_t t101 = gm_l19;
                            gm_l20 = t101;
                            {
                                {
                                    uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
                                    if (!t102) {
                                        goto gm_lb11;
                                    }
                                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                                    gm_l21 = t103;
                                    uint64_t t104 = gm_l19;
                                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint8_t t106 = (t104 >= t105);
                                    if (t106) {
                                        goto gm_lb6;
                                    }
                                    void * t107 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                    uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                    uint64_t t109 = gm_l19;
                                    if ((uintptr_t)t109 >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)t109, t108, gm_srckqas99ada0fa, 291, 23);
                                    uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)t107) + (uintptr_t)t109 * 4));
                                    uint64_t t111 = (uint64_t)t110;
                                    gm_l20 = t111;
                                    goto gm_lb10;
                                }
gm_lb11:;
                                uint64_t t112 = gm_l19;
                                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t114 = (t112 >= t113);
                                if (t114) {
                                    goto gm_lb6;
                                }
                            }
gm_lb10:;
                            gm_trqfvmjtt4sij t115;
                            gm_f_list_uvfu1k1((void *)&t115, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll81;
                            }
                            F.l22 = t115;
                            uint64_t t116 = gm_l19;
                            uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                            uint8_t t118 = (t116 < t117);
                            if (t118) {
                                void * t119 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint64_t t121 = gm_l19;
                                void * t122 = (void *)(((uint8_t *)t119) + (uintptr_t)t121 * 24);
                                uint8_t t123 = gm_f_is_default_spec_cdd8jng(t122);
                                uint8_t t124 = (uint8_t)!t123;
                                gm_l29 = t124;
                            } else {
                                gm_l29 = ((uint8_t)0);
                            }
                            uint8_t t125 = gm_l29;
                            gm_l23 = t125;
                            uint8_t t126 = gm_l23;
                            if (t126) {
                                void * t127 = gm_l0;
                                void * t128 = (void *)((uint8_t *)&F.l22);
                                void * t129 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t131 = gm_l20;
                                if ((uintptr_t)t131 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t131, t130, gm_srckqas99ada0fa, 298, 46);
                                gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t129) + (uintptr_t)t131 * 4));
                                void * t133 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint64_t t135 = gm_l20;
                                if ((uintptr_t)t135 >= (uintptr_t)(t134)) gm_trap_index((uintptr_t)t135, t134, gm_srckqas99ada0fa, 298, 54);
                                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)t133) + (uintptr_t)t135 * 16));
                                void * t137 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint64_t t139 = gm_l19;
                                if ((uintptr_t)t139 >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)t139, t138, gm_srckqas99ada0fa, 298, 64);
                                void * t140 = (void *)(((uint8_t *)t137) + (uintptr_t)t139 * 24);
                                gm_f_format_spec_7aph60t((void *)&gm_l24, (void *)NULL, cx, NULL, t127, t128, t132, t136, t140);
                                if (gm_unw.kind) {
                                    goto gm_ld83_1;
                                }
                            } else {
                                void * t141 = gm_l0;
                                void * t142 = (void *)((uint8_t *)&F.l22);
                                void * t143 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t145 = gm_l20;
                                if ((uintptr_t)t145 >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)t145, t144, gm_srckqas99ada0fa, 298, 101);
                                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t143) + (uintptr_t)t145 * 4));
                                void * t147 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint64_t t149 = gm_l20;
                                if ((uintptr_t)t149 >= (uintptr_t)(t148)) gm_trap_index((uintptr_t)t149, t148, gm_srckqas99ada0fa, 298, 109);
                                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)t147) + (uintptr_t)t149 * 16));
                                gm_f_format_value_vmle7qr((void *)&gm_l24, (void *)NULL, cx, NULL, t141, t142, t146, t150, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_ld83_1;
                                }
                            }
                            uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (t151) {
                            } else {
                                gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                                gm_l25 = t152;
                                {
                                    void * t153 = gm_l0;
                                    gm_tl874t2atnlh3 t154 = gm_l2;
                                    gm_b16_8 t155 = gm_l25;
                                    gm_t76123gfet73g t156;
                                    gm_f_trap_qh84l9d((void *)&t156, (void *)NULL, cx, NULL, t153, t154, t155);
                                    if (gm_unw.kind) {
                                        goto gm_ld83_1;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                    ((uint8_t *)&F.ret)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t156);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                    goto gm_ld83_1;
                                }
                            }
                            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                            uint8_t t158 = (t157 > ((uint64_t)0ULL));
                            if (t158) {
                                {
                                    void * t159 = gm_l0;
                                    uint32_t t160 = gm_l1;
                                    gm_tl874t2atnlh3 t161 = gm_l2;
                                    gm_b16_8 t162 = gm_l10;
                                    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                                    gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                                    gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                                    gm_tcgnlqgmuvh2q t166 = GM_LD(gm_tcgnlqgmuvh2q, ((uint8_t *)a3));
                                    gm_f_gen_emit_91g8h4i((void *)&gm_l30, (void *)NULL, cx, NULL, t159, t160, t161, &t166, t163, t165);
                                    if (gm_unw.kind) {
                                        goto gm_ld83_1;
                                    }
                                }
                                uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                if (t167) {
                                } else {
                                    gm_t76123gfet73g t168 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l30) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                    ((uint8_t *)&F.ret)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t168);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                    goto gm_ld83_1;
                                }
                            }
                        }
                        {
                            void * t169 = (void *)((uint8_t *)&F.l22);
                            gm_f_deinit_r70pji5(t169);
                        }
                        goto gm_ls170;
gm_ld83_1:;
                        {
                            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                            void * t171 = (void *)((uint8_t *)&F.l22);
                            gm_f_deinit_r70pji5(t171);
                            gm_unw = gm_saved;
                        }
                        goto gm_ll81;
gm_ls170:;
gm_lb9:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 286, 5);
                }
                goto gm_ls172;
gm_ll81:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741830) { gm_unw.kind = GM_NONE; goto gm_lb7; }
                goto gm_ll80;
gm_ls172:;
gm_lb7:;
            }
            goto gm_ls173;
gm_ll80:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741829) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            goto gm_ll76;
gm_ls173:;
gm_lb6:;
            uint64_t t174 = gm_l18;
            uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
            uint64_t t177 = r175;
            gm_l18 = t177;
        }
    }
    goto gm_ls178;
gm_ll76:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741827) { gm_unw.kind = GM_NONE; goto gm_lb4; }
    goto gm_lad;
gm_ls178:;
gm_lb4:;
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_slot_d_q5v1jqa(void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_up_get_n03r5d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, void * a5) {
    struct gm_fr_up_get_n03r5d3 F;
    void * gm_l0; /* m */
    void * gm_l1; /* up */
    uint32_t gm_l2; /* l */
    uint32_t gm_l3; /* rel */
    gm_b16_8 gm_l4; /* dst */
    void * gm_l5; /* n */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* o */
    uint64_t gm_l8; /* addr */
    GM_STACK_CHECK(&F, 605u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t833u93mr4iob gm_l9;
    gm_tcfa6l7reoosn gm_l10;
    uint32_t t4 = gm_l2;
    uint64_t t5 = (uint64_t)t4;
    gm_l6 = t5;
    {
        {
            void * t6 = gm_l1;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 64));
            uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 64) + 8));
            uint64_t t9 = gm_l6;
            if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_srckqas99ada0fa, 417, 8);
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)t9 * 8));
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l1;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 16));
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 120));
            uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 120) + 8));
            uint64_t t16 = gm_l6;
            if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srckqas99ada0fa, 417, 31);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)t16 * 1));
            uint8_t t18 = (uint8_t)!t17;
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = gm_l1;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 16));
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 72));
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 72) + 8));
            uint64_t t23 = gm_l6;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srckqas99ada0fa, 418, 17);
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
            uint64_t t25 = gm_f_us_cuv01fp(t24);
            uint32_t t26 = gm_l3;
            uint64_t t27 = (uint64_t)t26;
            uint64_t r28; bool o29 = __builtin_add_overflow(t25, t27, &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 418, 14);
            uint64_t t30 = r28;
            gm_l7 = t30;
            gm_b16_8 t31 = gm_l4;
            void * t32 = gm_l1;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 48));
            uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 48) + 8));
            uint64_t t35 = gm_l7;
            uint64_t t36 = gm_l7;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t r38; bool o39 = __builtin_add_overflow(t36, t37, &r38);
            if (o39) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 419, 35);
            uint64_t t40 = r38;
            uint64_t t41 = (uintptr_t)t35;
            uint64_t t42 = (uintptr_t)t40;
            if (t41 > t42 || t42 > (uintptr_t)t34) gm_trap_slice(t41, t42, t34, gm_srckqas99ada0fa, 419, 23);
            uint64_t t43 = ((uintptr_t)t40) - (uintptr_t)t35;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t33 + (uintptr_t)t35 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_f_copy_from_f7j6p05(t31, t45);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t46 = gm_l0;
        void * t47 = gm_l5;
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
        void * t49 = gm_l1;
        uint32_t t50 = gm_l2;
        gm_f_local_addr_o51qq3k((void *)&gm_l9, (void *)NULL, cx, NULL, t46, t48, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t51) {
    } else {
        gm_t76123gfet73g t52 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l9) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t52);
        goto gm_fdone;
    }
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    gm_l8 = t53;
    {
        void * t54 = gm_l0;
        void * t55 = gm_l5;
        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
        uint64_t t57 = gm_l8;
        uint32_t t58 = gm_l3;
        uint64_t t59 = (uint64_t)t58;
        uint64_t r60; bool o61 = __builtin_add_overflow(t57, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 423, 27);
        uint64_t t62 = r60;
        gm_b16_8 t63 = gm_l4;
        gm_f_load_at_6u81jcc((void *)&gm_l10, (void *)NULL, cx, NULL, t54, t56, t62, ((uint64_t)1ULL), t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (t64) {
    } else {
        gm_t76123gfet73g t65 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l10) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t65);
        goto gm_fdone;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) void gm_f_up_put_9i85d76(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, void * a5) {
    struct gm_fr_up_put_9i85d76 F;
    void * gm_l0; /* m */
    void * gm_l1; /* up */
    uint32_t gm_l2; /* l */
    uint32_t gm_l3; /* rel */
    gm_b16_8 gm_l4; /* data */
    void * gm_l5; /* n */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* o */
    uint64_t gm_l8; /* addr */
    GM_STACK_CHECK(&F, 589u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t833u93mr4iob gm_l9;
    gm_tcfa6l7reoosn gm_l10;
    uint32_t t4 = gm_l2;
    uint64_t t5 = (uint64_t)t4;
    gm_l6 = t5;
    {
        {
            void * t6 = gm_l1;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 64));
            uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 64) + 8));
            uint64_t t9 = gm_l6;
            if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_srckqas99ada0fa, 429, 8);
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)t9 * 8));
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l1;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 16));
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 120));
            uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 120) + 8));
            uint64_t t16 = gm_l6;
            if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srckqas99ada0fa, 429, 31);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t14) + (uintptr_t)t16 * 1));
            uint8_t t18 = (uint8_t)!t17;
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = gm_l1;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 16));
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 72));
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 72) + 8));
            uint64_t t23 = gm_l6;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srckqas99ada0fa, 430, 17);
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
            uint64_t t25 = gm_f_us_cuv01fp(t24);
            uint32_t t26 = gm_l3;
            uint64_t t27 = (uint64_t)t26;
            uint64_t r28; bool o29 = __builtin_add_overflow(t25, t27, &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 430, 14);
            uint64_t t30 = r28;
            gm_l7 = t30;
            void * t31 = gm_l1;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 48));
            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 48) + 8));
            uint64_t t34 = gm_l7;
            uint64_t t35 = gm_l7;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t r37; bool o38 = __builtin_add_overflow(t35, t36, &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 431, 21);
            uint64_t t39 = r37;
            uint64_t t40 = (uintptr_t)t34;
            uint64_t t41 = (uintptr_t)t39;
            if (t40 > t41 || t41 > (uintptr_t)t33) gm_trap_slice(t40, t41, t33, gm_srckqas99ada0fa, 431, 9);
            uint64_t t42 = ((uintptr_t)t39) - (uintptr_t)t34;
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t32 + (uintptr_t)t34 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
            gm_b16_8 t44 = gm_l4;
            gm_f_copy_from_f7j6p05(t43, t44);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t45 = gm_l0;
        void * t46 = gm_l5;
        gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 72));
        void * t48 = gm_l1;
        uint32_t t49 = gm_l2;
        gm_f_local_addr_o51qq3k((void *)&gm_l9, (void *)NULL, cx, NULL, t45, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t50) {
    } else {
        gm_t76123gfet73g t51 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l9) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t51);
        goto gm_fdone;
    }
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    gm_l8 = t52;
    {
        void * t53 = gm_l0;
        void * t54 = gm_l5;
        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
        uint64_t t56 = gm_l8;
        uint32_t t57 = gm_l3;
        uint64_t t58 = (uint64_t)t57;
        uint64_t r59; bool o60 = __builtin_add_overflow(t56, t58, &r59);
        if (o60) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 435, 28);
        uint64_t t61 = r59;
        gm_b16_8 t62 = gm_l4;
        gm_f_store_at_ktabiji((void *)&gm_l10, (void *)NULL, cx, NULL, t53, t55, t61, ((uint64_t)1ULL), t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (t63) {
    } else {
        gm_t76123gfet73g t64 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l10) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t64);
        goto gm_fdone;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
__attribute__((aligned(4))) void gm_f_vm_exit_518ppgp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3) {
    struct gm_fr_vm_exit_518ppgp F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* regs */
    void * gm_l3; /* x */
    gm_te9hrk0u35rev gm_l4; /* target */
    gm_tbl8il92l97ut gm_l5; /* t */
    gm_t9rfq1ocel0om gm_l6; /* v */
    gm_b16_8 gm_l7; /* d */
    gm_b16_8 gm_l8; /* b */
    uint64_t gm_l9; /* k */
    gm_te9hrk0u35rev gm_l10; /* target */
    gm_tbl8il92l97ut gm_l11; /* t */
    uint32_t gm_l12; /* levels */
    void * gm_l13; /* target */
    gm_t9rfq1ocel0om gm_l14; /* v */
    gm_b16_8 gm_l15; /* b */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* o */
    uint8_t gm_l18; /* is_err */
    GM_STACK_CHECK(&F, 1523u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9b5r56q9jk9c gm_l19;
    gm_th2lqbo77pvsc gm_l20;
    gm_t9b5r56q9jk9c gm_l21;
    void * gm_l22;
    void * t12 = gm_l3;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t12) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_te9hrk0u35rev t15 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)t12) + 4));
            gm_l4 = t15;
            {
                void * t16 = gm_l0;
                gm_tbl8il92l97ut t17 = gm_l1;
                gm_te9hrk0u35rev t18 = gm_l4;
                void * t19 = gm_l3;
                gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 36));
                gm_f_resolve_target_bdf5i1e((void *)&gm_l19, (void *)NULL, cx, NULL, t16, t17, t18, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (t21) {
            } else {
                gm_t76123gfet73g t22 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l19) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t22);
                goto gm_fdone;
            }
            gm_tbl8il92l97ut t23 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&gm_l19) + 8));
            gm_l5 = t23;
            {
                {
                    void * t24 = gm_l3;
                    uint8_t t25 = (uint8_t)(((((uint8_t *)t24) + 16))[12] != 0);
                    if (!t25) {
                        goto gm_lb4;
                    }
                    gm_t9rfq1ocel0om t26 = GM_LD(gm_t9rfq1ocel0om, (((uint8_t *)t24) + 16));
                    gm_l6 = t26;
                    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                    gm_f_label_dst_b5cat1e((void *)&gm_l20, t27, t28);
                    uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                    if (t29) {
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_l7 = t30;
                    } else {
                        void * t31 = gm_l0;
                        void * t32 = gm_l3;
                        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 36));
                        gm_b16_8 t34;
                        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s1kvbdtols1cc));
                        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(52));
                        gm_t76123gfet73g t35;
                        gm_f_trap_qh84l9d((void *)&t35, (void *)NULL, cx, NULL, t31, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t35);
                        goto gm_fdone;
                    }
                    void * t36 = gm_l2;
                    gm_t9rfq1ocel0om t37 = gm_l6;
                    gm_b16_8 t38 = gm_f_gen_bytes_bvo3jme(t36, t37);
                    gm_l8 = t38;
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t41 = (t39 < t40 ? t39 : t40);
                    gm_l9 = t41;
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t44 = gm_l9;
                    uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t46 = (uintptr_t)t44;
                    if (t45 > t46 || t46 > (uintptr_t)t43) gm_trap_slice(t45, t46, t43, gm_srckqas99ada0fa, 347, 17);
                    uint64_t t47 = ((uintptr_t)t44) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t42 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                    void * t49 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t51 = gm_l9;
                    uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t53 = (uintptr_t)t51;
                    if (t52 > t53 || t53 > (uintptr_t)t50) gm_trap_slice(t52, t53, t50, gm_srckqas99ada0fa, 347, 35);
                    uint64_t t54 = ((uintptr_t)t51) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t55;
                    GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                    gm_f_copy_from_f7j6p05(t48, t56);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                void * t57 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)t57));
                uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                gm_tn68el8d14vku t60;
                GM_ST(uint32_t, (uint8_t *)&t60 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t60 + 0), t58);
                GM_ST(uint32_t, ((uint8_t *)&t60 + 8), t59);
                gm_t76123gfet73g t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t61 + 8), t60);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t61);
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t12) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)1ULL));
            if (!t63) {
                goto gm_lb5;
            }
            gm_te9hrk0u35rev t64 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)t12) + 4));
            gm_l10 = t64;
            gm_te9hrk0u35rev t65 = gm_l10;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
            uint8_t t67 = (uint8_t)((t66 == ((uint8_t)1ULL)));
            gm_te9hrk0u35rev t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
            if (t67) {
                void * t69 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)t69));
                gm_tf5srh4mp4b3i t71;
                GM_ST(uint8_t, (uint8_t *)&t71 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t71 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t71 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t71 + 0), t70);
                GM_ST(uint8_t, ((uint8_t *)&t71 + 8), ((uint8_t)0));
                gm_t76123gfet73g t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t72 + 8), t71);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t72);
                goto gm_fdone;
            }
            {
                void * t73 = gm_l0;
                gm_tbl8il92l97ut t74 = gm_l1;
                gm_te9hrk0u35rev t75 = gm_l10;
                void * t76 = gm_l3;
                gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t76) + 36));
                gm_f_resolve_target_bdf5i1e((void *)&gm_l21, (void *)NULL, cx, NULL, t73, t74, t75, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (t78) {
            } else {
                gm_t76123gfet73g t79 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l21) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t79);
                goto gm_fdone;
            }
            gm_tbl8il92l97ut t80 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&gm_l21) + 8));
            gm_l11 = t80;
            {
                void * t81 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)t81));
                uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                gm_tn68el8d14vku t84;
                GM_ST(uint32_t, (uint8_t *)&t84 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t84 + 0), t82);
                GM_ST(uint32_t, ((uint8_t *)&t84 + 8), t83);
                gm_t76123gfet73g t85;
                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t85 + 8), t84);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t85);
                goto gm_fdone;
            }
        }
gm_lb5:;
        {
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t12) + 0));
            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)2ULL));
            if (!t87) {
                goto gm_lb6;
            }
            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)t12) + 4));
            gm_l12 = t88;
            void * t89 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint32_t t90 = gm_l12;
            void * t91 = gm_f_frame_up_ghkkuqg(t89, t90);
            gm_l22 = t91;
            uint8_t t92 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l22)) != 0);
            if (t92) {
                void * t93 = GM_LD(void *, ((uint8_t *)&gm_l22));
                gm_l13 = t93;
            } else {
                void * t94 = gm_l0;
                void * t95 = gm_l3;
                gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t95) + 36));
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sar5aak6f8jpo));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(43));
                gm_t76123gfet73g t98;
                gm_f_trap_qh84l9d((void *)&t98, (void *)NULL, cx, NULL, t94, t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t98);
                goto gm_fdone;
            }
            {
                {
                    void * t99 = gm_l3;
                    uint8_t t100 = (uint8_t)(((((uint8_t *)t99) + 16))[12] != 0);
                    if (!t100) {
                        goto gm_lb8;
                    }
                    gm_t9rfq1ocel0om t101 = GM_LD(gm_t9rfq1ocel0om, (((uint8_t *)t99) + 16));
                    gm_l14 = t101;
                    void * t102 = gm_l2;
                    gm_t9rfq1ocel0om t103 = gm_l14;
                    gm_b16_8 t104 = gm_f_gen_bytes_bvo3jme(t102, t103);
                    gm_l15 = t104;
                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    void * t106 = gm_l13;
                    uint64_t t107 = GM_LD(uint64_t, ((((uint8_t *)t106) + 24) + 8));
                    uint64_t t108 = (t105 < t107 ? t105 : t107);
                    gm_l16 = t108;
                    void * t109 = gm_l13;
                    void * t110 = GM_LD(void *, (((uint8_t *)t109) + 24));
                    uint64_t t111 = GM_LD(uint64_t, ((((uint8_t *)t109) + 24) + 8));
                    uint64_t t112 = gm_l16;
                    uint64_t t113 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t114 = (uintptr_t)t112;
                    if (t113 > t114 || t114 > (uintptr_t)t111) gm_trap_slice(t113, t114, t111, gm_srckqas99ada0fa, 365, 17);
                    uint64_t t115 = ((uintptr_t)t112) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(((uint8_t *)t110 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(t115));
                    void * t117 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t119 = gm_l16;
                    uint64_t t120 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t121 = (uintptr_t)t119;
                    if (t120 > t121 || t121 > (uintptr_t)t118) gm_trap_slice(t120, t121, t118, gm_srckqas99ada0fa, 365, 44);
                    uint64_t t122 = ((uintptr_t)t119) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t123;
                    GM_ST(void *, (uint8_t *)&t123, (void *)(((uint8_t *)t117 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(t122));
                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                    gm_f_copy_from_f7j6p05(t116, t124);
                    goto gm_lb7;
                }
gm_lb8:;
            }
gm_lb7:;
            {
                {
                    void * t125 = gm_l13;
                    void * t126 = GM_LD(void *, (((uint8_t *)t125) + 16));
                    uint8_t t127 = (uint8_t)(((((uint8_t *)t126) + 32))[8] != 0);
                    if (!t127) {
                        goto gm_lb10;
                    }
                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t126) + 32));
                    gm_l17 = t128;
                    void * t129 = gm_l13;
                    void * t130 = GM_LD(void *, (((uint8_t *)t129) + 24));
                    uint64_t t131 = GM_LD(uint64_t, ((((uint8_t *)t129) + 24) + 8));
                    uint64_t t132 = gm_l17;
                    uint64_t t133 = gm_f_us_cuv01fp(t132);
                    if ((uintptr_t)t133 >= (uintptr_t)(t131)) gm_trap_index((uintptr_t)t133, t131, gm_srckqas99ada0fa, 367, 54);
                    uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)t130) + (uintptr_t)t133 * 1));
                    uint8_t t135 = (!(t134 == ((uint8_t)0ULL)));
                    gm_l18 = t135;
                    goto gm_lb9;
                }
gm_lb10:;
                gm_l18 = ((uint8_t)0);
            }
gm_lb9:;
            {
                void * t136 = gm_l13;
                uint64_t t137 = GM_LD(uint64_t, ((uint8_t *)t136));
                uint8_t t138 = gm_l18;
                gm_tf5srh4mp4b3i t139;
                GM_ST(uint8_t, (uint8_t *)&t139 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t139 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t139 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t139 + 0), t137);
                GM_ST(uint8_t, ((uint8_t *)&t139 + 8), t138);
                gm_t76123gfet73g t140;
                GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t140 + 8), t139);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t140);
                goto gm_fdone;
            }
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 340, 5);
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
__attribute__((aligned(4))) void gm_f_vm_gen_qp72g0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, void * a4) {
    struct gm_fr_vm_gen_qp72g0e F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    void * gm_l2; /* fr */
    void * gm_l3; /* regs */
    void * gm_l4; /* g */
    gm_t2lqu8jsj511d gm_l5; /* mk */
    gm_b16_8 gm_l7; /* many */
    gm_b16_8 gm_l8; /* v */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* k */
    uint64_t gm_l12; /* n */
    gm_t2doi72kmmlil gm_l13; /* b */
    uint32_t gm_l14; /* at */
    gm_b16_8 gm_l15; /* d */
    gm_tl874t2atnlh3 gm_l16; /* span */
    gm_ts6oqppjh0ba3 gm_l17; /* op */
    gm_t88rcucg1b7uk gm_l18; /* lt */
    gm_t88rcucg1b7uk gm_l19; /* rt */
    gm_t9qkh0rsooc8d gm_l20; /* op */
    gm_t88rcucg1b7uk gm_l21; /* t */
    uint8_t gm_l22; /* negated */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint8_t gm_l24; /* x */
    uint8_t gm_l25; /* negated */
    gm_t88rcucg1b7uk gm_l26; /* et */
    gm_t88rcucg1b7uk gm_l27; /* ct */
    uint8_t gm_l28; /* x */
    gm_tmn7n0n9raou5 gm_l29; /* kind */
    gm_t88rcucg1b7uk gm_l30; /* from */
    uint8_t gm_l31; /* to_fn */
    uint8_t gm_l32; /* from_fn */
    uint64_t gm_l33; /* addr */
    gm_b16_8 gm_l34; /* offs */
    gm_t88rcucg1b7uk gm_l35; /* u */
    unsigned __int128 gm_l36; /* acc */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* i */
    gm_tplpbqf3duvn3 gm_l40; /* bf */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    uint64_t gm_l44; /* o */
    uint32_t gm_l45; /* index */
    gm_t88rcucg1b7uk gm_l46; /* t */
    uint32_t gm_l47; /* index */
    gm_tplpbqf3duvn3 gm_l48; /* bf */
    gm_t88rcucg1b7uk gm_l49; /* u */
    uint64_t gm_l50; /* k */
    unsigned __int128 gm_l51; /* whole */
    uint32_t gm_l52; /* w */
    unsigned __int128 gm_l53; /* mask */
    unsigned __int128 gm_l54; /* raw */
    uint64_t gm_l55; /* count */
    uint64_t gm_l56; /* k */
    uint64_t gm_l57; /* j */
    uint8_t gm_l58; /* x */
    gm_b16_8 gm_l59; /* bytes */
    uint8_t gm_l60; /* c */
    uint64_t gm_l61; /* addr */
    gm_t9qkh0rsooc8d gm_l62; /* kind */
    uint32_t gm_l63; /* variant */
    uint8_t gm_l64; /* inclusive */
    uint64_t gm_l65; /* esz */
    uint64_t gm_l66; /* lo */
    uint64_t gm_l67; /* hi */
    uint8_t gm_l68; /* inclusive */
    uint8_t gm_l69; /* has_len */
    uint64_t gm_l70; /* lo */
    uint64_t gm_l71; /* hi */
    gm_tf5srh4mp4b3i gm_l72; /* len */
    uint8_t gm_l73; /* bad */
    uint64_t gm_l74; /* l */
    uint64_t gm_l75; /* l */
    gm_b16_8 gm_l76; /* text */
    uint64_t gm_l77; /* i */
    uint64_t gm_l78; /* len */
    uint32_t gm_l79; /* variant */
    gm_t88rcucg1b7uk gm_l80; /* t */
    gm_t9805qtonbsag gm_l81; /* x */
    gm_b16_8 gm_l82; /* s */
    gm_b16_8 gm_l83; /* msg */
    gm_b16_8 gm_l84; /* what */
    gm_b16_8 gm_l86; /* pieces */
    gm_b16_8 gm_l87; /* specs */
    gm_b16_8 gm_l88; /* tys */
    uint8_t gm_l89; /* table */
    uint32_t gm_l90; /* f */
    void * gm_l91; /* f */
    gm_t88rcucg1b7uk gm_l92; /* fty */
    void * gm_l93; /* f */
    void * gm_l94; /* f */
    gm_t4qfihdig0mro gm_l95; /* c */
    gm_b16_8 gm_l96; /* saved */
    void * gm_l97; /* f */
    gm_t4qfihdig0mro gm_l98; /* c */
    uint32_t gm_l99; /* r */
    GM_STACK_CHECK(&F, 9179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l100;
    gm_b16_8 gm_l101;
    gm_tcfa6l7reoosn gm_l102;
    gm_tcfa6l7reoosn gm_l103;
    gm_t1ec8br9ddkav gm_l104;
    uint8_t gm_l105;
    gm_t1ec8br9ddkav gm_l106;
    uint8_t gm_l107;
    gm_t53at80pfdus3 gm_l108;
    gm_t53at80pfdus3 gm_l109;
    uint8_t gm_l110;
    gm_t9805qtonbsag gm_l111;
    gm_tcfa6l7reoosn gm_l112;
    gm_tkn42u7na3t81 gm_l113;
    gm_tkn42u7na3t81 gm_l114;
    unsigned __int128 gm_l115;
    gm_t833u93mr4iob gm_l116;
    gm_b16_8 gm_l117;
    gm_b16_8 gm_l118;
    gm_tabb710d9mq2e gm_l119;
    gm_tabb710d9mq2e gm_l120;
    gm_tabb710d9mq2e gm_l121;
    uint8_t gm_l122;
    gm_td0ept12e7q8b gm_l123;
    gm_tcfa6l7reoosn gm_l124;
    gm_tcfa6l7reoosn gm_l125;
    gm_tmr8tdh5g7uj6 gm_l126;
    gm_tabb710d9mq2e gm_l127;
    gm_tmr8tdh5g7uj6 gm_l128;
    gm_tcfa6l7reoosn gm_l129;
    {
        {
            {
                void * t88 = gm_l0;
                gm_t2lqu8jsj511d t89 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t88));
                gm_l5 = t89;
                gm_b16_8 t90 = gm_f_empty_slice_ijtp9lo();
                gm_tdmi4fk9nhqa7 t91;
                for (size_t i92 = 0; i92 < 4; i92++) GM_ST(gm_b16_8, (uint8_t *)&t91 + i92 * 16, t90);
                F.l6 = t91;
                gm_b16_8 t93 = gm_f_empty_slice_2p7d2kv();
                gm_l7 = t93;
                void * t94 = gm_l4;
                uint64_t t95 = GM_LD(uint64_t, ((((uint8_t *)t94) + 104) + 8));
                uint8_t t96 = (t95 > ((uint64_t)4ULL));
                if (t96) {
                    {
                        void * t97 = gm_l4;
                        uint64_t t98 = GM_LD(uint64_t, ((((uint8_t *)t97) + 104) + 8));
                        gm_b16_8 t99 = gm_f_empty_slice_ijtp9lo();
                        gm_f_make_fill_f96auc0((void *)&gm_l100, (void *)NULL, cx, NULL, t98, t99, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdicmjcqqa3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                    if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 8, gm_srckqas99ada0fa, 106, 32);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                    gm_l7 = t101;
                }
                void * t102 = gm_l4;
                uint64_t t103 = GM_LD(uint64_t, ((((uint8_t *)t102) + 104) + 8));
                uint8_t t104 = (t103 > ((uint64_t)4ULL));
                if (t104) {
                    gm_b16_8 t105 = gm_l7;
                    gm_l8 = t105;
                } else {
                    void * t106 = (void *)((uint8_t *)&F.l6);
                    void * t107 = gm_l4;
                    uint64_t t108 = GM_LD(uint64_t, ((((uint8_t *)t107) + 104) + 8));
                    uint64_t t109 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t110 = (uintptr_t)t108;
                    if (t109 > t110 || t110 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t109, t110, ((uint64_t)4ULL), gm_srckqas99ada0fa, 108, 51);
                    uint64_t t111 = ((uintptr_t)t108) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t112;
                    GM_ST(void *, (uint8_t *)&t112, (void *)(((uint8_t *)t106 + (uintptr_t)((uint64_t)0ULL) * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(t111));
                    gm_l8 = t112;
                }
                gm_l10 = ((uint64_t)0ULL);
                void * t113 = gm_l4;
                uint64_t t114 = GM_LD(uint64_t, ((((uint8_t *)t113) + 104) + 8));
                gm_l9 = t114;
                {
                    for (;;) {
                        uint64_t t115 = gm_l10;
                        uint64_t t116 = gm_l9;
                        uint8_t t117 = (t115 < t116);
                        if (!t117) {
                            goto gm_lb2;
                        }
                        {
                            {
                                {
                                    uint64_t t118 = gm_l10;
                                    gm_l11 = t118;
                                    void * t119 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t121 = gm_l11;
                                    if ((uintptr_t)t121 >= (uintptr_t)(t120)) gm_trap_index((uintptr_t)t121, t120, gm_srckqas99ada0fa, 109, 30);
                                    void * t122 = gm_l3;
                                    void * t123 = gm_l4;
                                    void * t124 = GM_LD(void *, (((uint8_t *)t123) + 104));
                                    uint64_t t125 = GM_LD(uint64_t, ((((uint8_t *)t123) + 104) + 8));
                                    uint64_t t126 = gm_l11;
                                    if ((uintptr_t)t126 >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)t126, t125, gm_srckqas99ada0fa, 109, 53);
                                    gm_t9rfq1ocel0om t127 = GM_LD(gm_t9rfq1ocel0om, (((uint8_t *)t124) + (uintptr_t)t126 * 12));
                                    gm_b16_8 t128 = gm_f_gen_bytes_bvo3jme(t122, t127);
                                    GM_ST(gm_b16_8, (((uint8_t *)t119) + (uintptr_t)t121 * 16), t128);
                                    goto gm_lb5;
                                }
gm_lb6:;
                                gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 109, 5);
                            }
gm_lb5:;
                        }
gm_lb4:;
                        uint64_t t129 = gm_l10;
                        uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                        uint64_t t132 = r130;
                        gm_l10 = t132;
                    }
                }
gm_lb2:;
            }
            {
                void * t133 = gm_l0;
                void * t134 = gm_l4;
                gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t134) + 128));
                uint64_t t136 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t133, t135);
                if (gm_unw.kind) {
                    goto gm_ld87_1;
                }
                gm_l12 = t136;
                gm_t2doi72kmmlil t137;
                GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 1), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 2), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 3), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 4), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 5), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 6), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t137 + 7), ((uint8_t)0ULL));
                gm_l13 = t137;
                void * t138 = gm_l4;
                {
                    {
                        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t138) + 120) + 0));
                        uint8_t t140 = (uint8_t)(t139 == ((uint8_t)1ULL));
                        if (!t140) {
                            goto gm_lb8;
                        }
                        void * t141 = (void *)((uint8_t *)&gm_l13);
                        uint64_t t142 = gm_l12;
                        uint64_t t143 = (t142 < ((uint64_t)8ULL) ? t142 : ((uint64_t)8ULL));
                        uint64_t t144 = gm_f_us_cuv01fp(t143);
                        uint64_t t145 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t146 = (uintptr_t)t144;
                        if (t145 > t146 || t146 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t145, t146, ((uint64_t)8ULL), gm_srckqas99ada0fa, 113, 20);
                        uint64_t t147 = ((uintptr_t)t144) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t148;
                        GM_ST(void *, (uint8_t *)&t148, (void *)(((uint8_t *)t141 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(t147));
                        gm_l15 = t148;
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t138) + 120) + 0));
                        uint8_t t150 = (uint8_t)(t149 == ((uint8_t)2ULL));
                        if (!t150) {
                            goto gm_lb9;
                        }
                        uint32_t t151 = GM_LD(uint32_t, ((((uint8_t *)t138) + 120) + 4));
                        gm_l14 = t151;
                        void * t152 = gm_l3;
                        uint32_t t153 = gm_l14;
                        uint64_t t154 = (uint64_t)t153;
                        void * t155 = gm_f_fat_pnqciib(t152, t154);
                        uint64_t t156 = gm_l12;
                        uint64_t t157 = gm_f_us_cuv01fp(t156);
                        uint64_t t158 = ((uintptr_t)t157) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t159;
                        GM_ST(void *, (uint8_t *)&t159, (void *)(((uint8_t *)t155 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(t158));
                        gm_l15 = t159;
                        goto gm_lb7;
                    }
gm_lb9:;
                    {
                        uint64_t t160 = gm_l12;
                        uint8_t t161 = (t160 <= ((uint64_t)8ULL));
                        if (t161) {
                            void * t162 = (void *)((uint8_t *)&gm_l13);
                            uint64_t t163 = gm_l12;
                            uint64_t t164 = gm_f_us_cuv01fp(t163);
                            uint64_t t165 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t166 = (uintptr_t)t164;
                            if (t165 > t166 || t166 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t165, t166, ((uint64_t)8ULL), gm_srckqas99ada0fa, 115, 26);
                            uint64_t t167 = ((uintptr_t)t164) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t168;
                            GM_ST(void *, (uint8_t *)&t168, (void *)(((uint8_t *)t162 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(t167));
                            gm_l101 = t168;
                        } else {
                            void * t169 = gm_l0;
                            uint64_t t170 = gm_l12;
                            gm_b16_8 t171 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t169, t170);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            gm_l101 = t171;
                        }
                        gm_b16_8 t172 = gm_l101;
                        gm_l15 = t172;
                        goto gm_lb7;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 112, 15);
                }
gm_lb7:;
                void * t173 = gm_l4;
                gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 132));
                gm_l16 = t174;
                void * t175 = gm_l4;
                {
                    {
                        {
                            uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t177 = (uint8_t)(t176 == ((uint8_t)0ULL));
                            if (!t177) {
                                goto gm_lb12;
                            }
                            gm_ts6oqppjh0ba3 t178 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)t175) + 8));
                            gm_l17 = t178;
                            gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l18 = t179;
                            gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 8));
                            gm_l19 = t180;
                            void * t181 = gm_l0;
                            gm_tl874t2atnlh3 t182 = gm_l16;
                            gm_ts6oqppjh0ba3 t183 = gm_l17;
                            gm_t88rcucg1b7uk t184 = gm_l18;
                            gm_t88rcucg1b7uk t185 = gm_l19;
                            void * t186 = gm_l4;
                            gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t186) + 128));
                            void * t188 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t189)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t189, gm_srckqas99ada0fa, 119, 65);
                            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)t188) + (uintptr_t)((uint64_t)0ULL) * 16));
                            void * t191 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t192)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t192, gm_srckqas99ada0fa, 119, 71);
                            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)t191) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t194 = gm_l15;
                            gm_f_binop_nrsq3mp((void *)&gm_l102, (void *)NULL, cx, NULL, t181, t182, t183, t184, t185, t187, t190, t193, t194);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                            if (t195) {
                            } else {
                                gm_t76123gfet73g t196 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l102) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t196);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t198 = (uint8_t)(t197 == ((uint8_t)1ULL));
                            if (!t198) {
                                goto gm_lb13;
                            }
                            gm_t9qkh0rsooc8d t199 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t175) + 8));
                            gm_l20 = t199;
                            gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l21 = t200;
                            void * t201 = gm_l0;
                            gm_tl874t2atnlh3 t202 = gm_l16;
                            gm_t9qkh0rsooc8d t203 = gm_l20;
                            gm_t88rcucg1b7uk t204 = gm_l21;
                            void * t205 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t206)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t206, gm_srckqas99ada0fa, 120, 47);
                            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)t205) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
                            gm_b16_8 t209 = gm_l15;
                            gm_f_unop_qamfooj((void *)&gm_l103, (void *)NULL, cx, NULL, t201, t202, t203, t204, t208, t209);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                            if (t210) {
                            } else {
                                gm_t76123gfet73g t211 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l103) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t211);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb13:;
                        {
                            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t213 = (uint8_t)(t212 == ((uint8_t)2ULL));
                            if (!t213) {
                                goto gm_lb14;
                            }
                            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)t175) + 8));
                            gm_l22 = t214;
                            gm_t88rcucg1b7uk t215 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l23 = t215;
                            void * t216 = gm_l0;
                            gm_tl874t2atnlh3 t217 = gm_l16;
                            gm_t88rcucg1b7uk t218 = gm_l23;
                            void * t219 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t220)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t220, gm_srckqas99ada0fa, 122, 39);
                            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t219) + (uintptr_t)((uint64_t)0ULL) * 16));
                            void * t222 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t223)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t223, gm_srckqas99ada0fa, 122, 45);
                            gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)t222) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_f_equal_e5vf82h((void *)&gm_l104, (void *)NULL, cx, NULL, t216, t217, t218, t221, t224);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l104))[0] != 0);
                            if (t225) {
                            } else {
                                gm_t76123gfet73g t226 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l104) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t226);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l104) + 8));
                            gm_l24 = t227;
                            void * t228 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t229)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t229, gm_srckqas99ada0fa, 123, 13);
                            uint8_t t230 = gm_l24;
                            uint8_t t231 = gm_l22;
                            uint8_t t232 = (!(t230 == t231));
                            if (t232) {
                                gm_l105 = ((uint8_t)1ULL);
                            } else {
                                gm_l105 = ((uint8_t)0ULL);
                            }
                            uint8_t t233 = gm_l105;
                            GM_ST(uint8_t, (((uint8_t *)t228) + (uintptr_t)((uint64_t)0ULL) * 1), t233);
                            goto gm_lb11;
                        }
gm_lb14:;
                    }
                    {
                        {
                            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t235 = (uint8_t)(t234 == ((uint8_t)3ULL));
                            if (!t235) {
                                goto gm_lb15;
                            }
                            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)t175) + 8));
                            gm_l25 = t236;
                            gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l26 = t237;
                            gm_t88rcucg1b7uk t238 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 8));
                            gm_l27 = t238;
                            void * t239 = gm_l0;
                            gm_tl874t2atnlh3 t240 = gm_l16;
                            gm_t88rcucg1b7uk t241 = gm_l26;
                            void * t242 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t243, gm_srckqas99ada0fa, 126, 43);
                            gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)t242) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_t88rcucg1b7uk t245 = gm_l27;
                            void * t246 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t247, gm_srckqas99ada0fa, 126, 53);
                            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)t246) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_f_contains_nugmipn((void *)&gm_l106, (void *)NULL, cx, NULL, t239, t240, t241, t244, t245, t248);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                            if (t249) {
                            } else {
                                gm_t76123gfet73g t250 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l106) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t250);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 8));
                            gm_l28 = t251;
                            void * t252 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t253)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t253, gm_srckqas99ada0fa, 127, 13);
                            uint8_t t254 = gm_l28;
                            uint8_t t255 = gm_l25;
                            uint8_t t256 = (!(t254 == t255));
                            if (t256) {
                                gm_l107 = ((uint8_t)1ULL);
                            } else {
                                gm_l107 = ((uint8_t)0ULL);
                            }
                            uint8_t t257 = gm_l107;
                            GM_ST(uint8_t, (((uint8_t *)t252) + (uintptr_t)((uint64_t)0ULL) * 1), t257);
                            goto gm_lb11;
                        }
gm_lb15:;
                        {
                            uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t259 = (uint8_t)(t258 == ((uint8_t)4ULL));
                            if (!t259) {
                                goto gm_lb16;
                            }
                            gm_tmn7n0n9raou5 t260 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)t175) + 8));
                            gm_l29 = t260;
                            gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 8));
                            gm_l30 = t261;
                            {
                                {
                                    uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                    uint8_t t263 = (uint8_t)(t262 == ((uint8_t)5ULL));
                                    if (!t263) {
                                        goto gm_lb18;
                                    }
                                    {
                                        void * t264 = gm_l0;
                                        gm_tl874t2atnlh3 t265 = gm_l16;
                                        gm_b16_8 t266;
                                        GM_ST(void *, (uint8_t *)&t266, (void *)(gm_s76i2uidi0pjj));
                                        GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(53));
                                        gm_t76123gfet73g t267;
                                        gm_f_trap_qh84l9d((void *)&t267, (void *)NULL, cx, NULL, t264, t265, t266);
                                        if (gm_unw.kind) {
                                            goto gm_ld87_1;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                        ((uint8_t *)&F.ret)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t267);
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                        goto gm_ld87_1;
                                    }
                                }
gm_lb18:;
                                {
                                    uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)9ULL));
                                    if (!t269) {
                                        goto gm_lb19;
                                    }
                                    {
                                        void * t270 = gm_l0;
                                        void * t271 = gm_l4;
                                        gm_t88rcucg1b7uk t272 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t271) + 128));
                                        gm_f_kind_htv6093((void *)&gm_l108, t270, GM_CONV(gm_csguankko4lmhd, t272));
                                    }
                                    {
                                        {
                                            uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l108) + 0));
                                            uint8_t t274 = (uint8_t)(t273 == ((uint8_t)15ULL));
                                            if (!t274) {
                                                goto gm_lb21;
                                            }
                                            gm_l31 = ((uint8_t)1);
                                            goto gm_lb20;
                                        }
gm_lb21:;
                                        {
                                            gm_l31 = ((uint8_t)0);
                                            goto gm_lb20;
                                        }
gm_lb22:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 132, 26);
                                    }
gm_lb20:;
                                    {
                                        void * t275 = gm_l0;
                                        gm_t88rcucg1b7uk t276 = gm_l30;
                                        gm_f_kind_htv6093((void *)&gm_l109, t275, GM_CONV(gm_csguankko4lmhd, t276));
                                    }
                                    {
                                        {
                                            uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l109) + 0));
                                            uint8_t t278 = (uint8_t)(t277 == ((uint8_t)15ULL));
                                            if (!t278) {
                                                goto gm_lb24;
                                            }
                                            gm_l32 = ((uint8_t)1);
                                            goto gm_lb23;
                                        }
gm_lb24:;
                                        {
                                            gm_l32 = ((uint8_t)0);
                                            goto gm_lb23;
                                        }
gm_lb25:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 133, 28);
                                    }
gm_lb23:;
                                    {
                                        {
                                            uint8_t t279 = gm_l31;
                                            if (!t279) {
                                                goto gm_lb27;
                                            }
                                            uint8_t t280 = gm_l32;
                                            uint8_t t281 = (uint8_t)!t280;
                                            if (!t281) {
                                                goto gm_lb27;
                                            }
                                            void * t282 = gm_l0;
                                            void * t283 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                            uint64_t t284 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t284)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t284, gm_srckqas99ada0fa, 135, 38);
                                            gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)t283) + (uintptr_t)((uint64_t)0ULL) * 16));
                                            gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                                            uint64_t t287 = gm_f_ptr_of_b77b4c9(t282, t286);
                                            gm_l33 = t287;
                                            void * t288 = gm_l0;
                                            uint64_t t289 = gm_l33;
                                            gm_t9805qtonbsag t290 = gm_f_fn_from_addr_7a6bpqd(t288, t289);
                                            gm_l111 = t290;
                                            {
                                                {
                                                    uint8_t t291 = (uint8_t)((((uint8_t *)&gm_l111))[4] != 0);
                                                    if (t291) {
                                                        goto gm_lb29;
                                                    }
                                                    gm_l110 = ((uint8_t)1);
                                                    goto gm_lb28;
                                                }
gm_lb29:;
                                                {
                                                    gm_l110 = ((uint8_t)0);
                                                    goto gm_lb28;
                                                }
gm_lb30:;
                                                gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 136, 24);
                                            }
gm_lb28:;
                                            uint8_t t292 = gm_l110;
                                            if (t292) {
                                                void * t293 = gm_l0;
                                                gm_tl874t2atnlh3 t294 = gm_l16;
                                                gm_b16_8 t295;
                                                GM_ST(void *, (uint8_t *)&t295, (void *)(gm_ss1mi142k161m));
                                                GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(40));
                                                gm_t76123gfet73g t296;
                                                gm_f_trap_qh84l9d((void *)&t296, (void *)NULL, cx, NULL, t293, t294, t295);
                                                if (gm_unw.kind) {
                                                    goto gm_ld87_1;
                                                }
                                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                                ((uint8_t *)&F.ret)[0] = 1;
                                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t296);
                                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                                goto gm_ld87_1;
                                            }
                                            void * t297 = gm_l0;
                                            gm_b16_8 t298 = gm_l15;
                                            uint64_t t299 = gm_l33;
                                            gm_f_put_word_tfn443i(t297, t298, ((uint64_t)0ULL), t299);
                                            void * t300 = gm_l0;
                                            gm_b16_8 t301 = gm_l15;
                                            gm_f_put_word_tfn443i(t300, t301, ((uint64_t)1ULL), ((uint64_t)0ULL));
                                            void * t302 = gm_l0;
                                            gm_b16_8 t303 = gm_l15;
                                            uint32_t t304 = gm_l1;
                                            uint64_t t305 = (uint64_t)t304;
                                            gm_f_put_word_tfn443i(t302, t303, ((uint64_t)2ULL), t305);
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                        {
                                            {
                                                uint8_t t306 = gm_l31;
                                                uint8_t t307 = (uint8_t)!t306;
                                                if (!t307) {
                                                    goto gm_lb32;
                                                }
                                                uint8_t t308 = gm_l32;
                                                if (!t308) {
                                                    goto gm_lb32;
                                                }
                                                void * t309 = gm_l0;
                                                gm_b16_8 t310 = gm_l15;
                                                void * t311 = gm_l0;
                                                void * t312 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t313)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t313, gm_srckqas99ada0fa, 141, 41);
                                                gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)t312) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                                                uint64_t t316 = gm_f_word_o2rcrhs(t311, t315, ((uint64_t)0ULL));
                                                gm_f_put_ptr_188p3mm(t309, t310, t316);
                                                goto gm_lb31;
                                            }
gm_lb32:;
                                            gm_b16_8 t317 = gm_l15;
                                            void * t318 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                            uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t319)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t319, gm_srckqas99ada0fa, 143, 33);
                                            void * t320 = GM_LD(void *, (((uint8_t *)t318) + (uintptr_t)((uint64_t)0ULL) * 16));
                                            uint64_t t321 = GM_LD(uint64_t, ((((uint8_t *)t318) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                            uint64_t t322 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t323 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t324 = (uintptr_t)t322;
                                            if (t323 > t324 || t324 > (uintptr_t)t321) gm_trap_slice(t323, t324, t321, gm_srckqas99ada0fa, 143, 33);
                                            uint64_t t325 = ((uintptr_t)t322) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t326;
                                            GM_ST(void *, (uint8_t *)&t326, (void *)(((uint8_t *)t320 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(t325));
                                            gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                                            gm_f_copy_from_f7j6p05(t317, t327);
                                        }
gm_lb31:;
                                    }
gm_lb26:;
                                    goto gm_lb17;
                                }
gm_lb19:;
                                {
                                    {
                                        {
                                            uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                            uint8_t t329 = (uint8_t)(t328 == ((uint8_t)6ULL));
                                            if (!t329) {
                                                goto gm_lb35;
                                            }
                                            goto gm_lb34;
                                        }
gm_lb35:;
                                        {
                                            uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                            uint8_t t331 = (uint8_t)(t330 == ((uint8_t)8ULL));
                                            if (!t331) {
                                                goto gm_lb36;
                                            }
                                            goto gm_lb34;
                                        }
gm_lb36:;
                                        {
                                            uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                            uint8_t t333 = (uint8_t)(t332 == ((uint8_t)10ULL));
                                            if (!t333) {
                                                goto gm_lb37;
                                            }
                                            goto gm_lb34;
                                        }
gm_lb37:;
                                        goto gm_lb33;
                                    }
gm_lb34:;
                                    gm_b16_8 t334 = gm_l15;
                                    void * t335 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t336 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t336)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t336, gm_srckqas99ada0fa, 146, 65);
                                    void * t337 = GM_LD(void *, (((uint8_t *)t335) + (uintptr_t)((uint64_t)0ULL) * 16));
                                    uint64_t t338 = GM_LD(uint64_t, ((((uint8_t *)t335) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                    uint64_t t339 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint64_t t340 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t341 = (uintptr_t)t339;
                                    if (t340 > t341 || t341 > (uintptr_t)t338) gm_trap_slice(t340, t341, t338, gm_srckqas99ada0fa, 146, 65);
                                    uint64_t t342 = ((uintptr_t)t339) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t343;
                                    GM_ST(void *, (uint8_t *)&t343, (void *)(((uint8_t *)t337 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(t342));
                                    gm_b16_8 t344 = GM_LD(gm_b16_8, ((uint8_t *)&t343));
                                    gm_f_copy_from_f7j6p05(t334, t344);
                                    goto gm_lb17;
                                }
gm_lb33:;
                                {
                                    void * t345 = gm_l0;
                                    gm_tl874t2atnlh3 t346 = gm_l16;
                                    gm_tmn7n0n9raou5 t347 = gm_l29;
                                    gm_t88rcucg1b7uk t348 = gm_l30;
                                    void * t349 = gm_l4;
                                    gm_t88rcucg1b7uk t350 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t349) + 128));
                                    void * t351 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t352)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t352, gm_srckqas99ada0fa, 147, 61);
                                    gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)t351) + (uintptr_t)((uint64_t)0ULL) * 16));
                                    gm_b16_8 t354 = gm_l15;
                                    gm_f_coerce_bytes_9kf3u24((void *)&gm_l112, (void *)NULL, cx, NULL, t345, t346, t347, t348, t350, t353, t354);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    uint8_t t355 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                                    if (t355) {
                                    } else {
                                        gm_t76123gfet73g t356 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l112) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                        ((uint8_t *)&F.ret)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t356);
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                        goto gm_ld87_1;
                                    }
                                    goto gm_lb17;
                                }
gm_lb38:;
                                gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 129, 32);
                            }
gm_lb17:;
                            goto gm_lb11;
                        }
gm_lb16:;
                    }
                    {
                        {
                            uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t358 = (uint8_t)(t357 == ((uint8_t)5ULL));
                            if (!t358) {
                                goto gm_lb39;
                            }
                            gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 8));
                            gm_l34 = t359;
                            {
                                {
                                    void * t360 = gm_l0;
                                    void * t361 = gm_l4;
                                    gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t361) + 128));
                                    gm_tkn42u7na3t81 t363 = gm_f_bits_struct_ty_22ue20s(t360, GM_CONV(gm_csguankko4lmhd, t362));
                                    gm_l113 = t363;
                                    uint8_t t364 = (uint8_t)((((uint8_t *)&gm_l113))[4] != 0);
                                    if (!t364) {
                                        goto gm_lb41;
                                    }
                                    gm_t88rcucg1b7uk t365 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l113));
                                    gm_l35 = t365;
                                    gm_l36 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                                    gm_l38 = ((uint64_t)0ULL);
                                    uint64_t t366 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    gm_l37 = t366;
                                    {
                                        for (;;) {
                                            uint64_t t367 = gm_l38;
                                            uint64_t t368 = gm_l37;
                                            uint8_t t369 = (t367 < t368);
                                            if (!t369) {
                                                goto gm_lb42;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t370 = gm_l38;
                                                        gm_l39 = t370;
                                                        void * t371 = gm_l0;
                                                        void * t372 = gm_l4;
                                                        gm_t88rcucg1b7uk t373 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t372) + 128));
                                                        uint64_t t374 = gm_l39;
                                                        uint32_t t375 = (uint32_t)t374;
                                                        gm_tplpbqf3duvn3 t376 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t371, t373, t375);
                                                        if (gm_unw.kind) {
                                                            goto gm_ld87_1;
                                                        }
                                                        gm_l40 = t376;
                                                        unsigned __int128 t377 = gm_l36;
                                                        void * t378 = gm_l0;
                                                        gm_t88rcucg1b7uk t379 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l40) + 4));
                                                        void * t380 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                        uint64_t t381 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                        uint64_t t382 = gm_l39;
                                                        gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)t380) + (uintptr_t)t382 * 16));
                                                        gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
                                                        unsigned __int128 t385 = gm_f_bits_raw_t6dqs65((void *)NULL, cx, NULL, t378, t379, t384);
                                                        if (gm_unw.kind) {
                                                            goto gm_ld87_1;
                                                        }
                                                        uint32_t t386 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                                        unsigned __int128 t387 = (unsigned __int128)t386;
                                                        unsigned __int128 n388 = (unsigned __int128)t387;
                                                        if (n388 >= 128) gm_trap_shift((uint64_t)n388, "u128", gm_srckqas99ada0fa, 154, 28);
                                                        unsigned __int128 r389 = (unsigned __int128)((unsigned __int128)t385 << (int)n388);
                                                        bool o390 = (unsigned __int128)(r389 >> (int)n388) != t385;
                                                        if (o390) gm_trap("integer overflow in `<<`", gm_srckqas99ada0fa, 154, 28);
                                                        unsigned __int128 t391 = r389;
                                                        unsigned __int128 t392 = (unsigned __int128)(t377 | t391);
                                                        gm_l36 = t392;
                                                        goto gm_lb45;
                                                    }
gm_lb46:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 152, 17);
                                                }
gm_lb45:;
                                            }
gm_lb44:;
                                            uint64_t t393 = gm_l38;
                                            uint64_t r394; bool o395 = __builtin_add_overflow(t393, ((uint64_t)1ULL), &r394);
                                            uint64_t t396 = r394;
                                            gm_l38 = t396;
                                        }
                                    }
gm_lb42:;
                                    void * t397 = gm_l0;
                                    void * t398 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                    uint64_t t399 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    void * t400 = gm_l0;
                                    gm_t88rcucg1b7uk t401 = gm_l35;
                                    uint64_t t402 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t400, t401);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    uint64_t t403 = gm_f_us_cuv01fp(t402);
                                    uint64_t t404 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t405 = (uintptr_t)t403;
                                    if (t404 > t405 || t405 > (uintptr_t)t399) gm_trap_slice(t404, t405, t399, gm_srckqas99ada0fa, 156, 28);
                                    uint64_t t406 = ((uintptr_t)t403) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t407;
                                    GM_ST(void *, (uint8_t *)&t407, (void *)(((uint8_t *)t398 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t407 + 8, (uintptr_t)(t406));
                                    unsigned __int128 t408 = gm_l36;
                                    gm_f_put_uint_cddin99(t397, t407, t408);
                                    goto gm_lb40;
                                }
gm_lb41:;
                                gm_l42 = ((uint64_t)0ULL);
                                uint64_t t409 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                gm_l41 = t409;
                                {
                                    for (;;) {
                                        uint64_t t410 = gm_l42;
                                        uint64_t t411 = gm_l41;
                                        uint8_t t412 = (t410 < t411);
                                        if (!t412) {
                                            goto gm_lb47;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t413 = gm_l42;
                                                    gm_l43 = t413;
                                                    void * t414 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                                    uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                                    uint64_t t416 = gm_l43;
                                                    if ((uintptr_t)t416 >= (uintptr_t)(t415)) gm_trap_index((uintptr_t)t416, t415, gm_srckqas99ada0fa, 159, 29);
                                                    uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)t414) + (uintptr_t)t416 * 8));
                                                    uint64_t t418 = gm_f_us_cuv01fp(t417);
                                                    gm_l44 = t418;
                                                    void * t419 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                    uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                    uint64_t t421 = gm_l44;
                                                    uint64_t t422 = gm_l44;
                                                    void * t423 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                    uint64_t t424 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                    uint64_t t425 = gm_l43;
                                                    uint64_t t426 = GM_LD(uint64_t, ((((uint8_t *)t423) + (uintptr_t)t425 * 16) + 8));
                                                    uint64_t r427; bool o428 = __builtin_add_overflow(t422, t426, &r427);
                                                    if (o428) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 160, 26);
                                                    uint64_t t429 = r427;
                                                    uint64_t t430 = (uintptr_t)t421;
                                                    uint64_t t431 = (uintptr_t)t429;
                                                    if (t430 > t431 || t431 > (uintptr_t)t420) gm_trap_slice(t430, t431, t420, gm_srckqas99ada0fa, 160, 21);
                                                    uint64_t t432 = ((uintptr_t)t429) - (uintptr_t)t421;
                                                    gm_b16_8 t433;
                                                    GM_ST(void *, (uint8_t *)&t433, (void *)(((uint8_t *)t419 + (uintptr_t)t421 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t433 + 8, (uintptr_t)(t432));
                                                    void * t434 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                    uint64_t t435 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                    uint64_t t436 = gm_l43;
                                                    gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)t434) + (uintptr_t)t436 * 16));
                                                    gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&t437));
                                                    gm_f_copy_from_f7j6p05(t433, t438);
                                                    goto gm_lb50;
                                                }
gm_lb51:;
                                                gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 158, 17);
                                            }
gm_lb50:;
                                        }
gm_lb49:;
                                        uint64_t t439 = gm_l42;
                                        uint64_t r440; bool o441 = __builtin_add_overflow(t439, ((uint64_t)1ULL), &r440);
                                        uint64_t t442 = r440;
                                        gm_l42 = t442;
                                    }
                                }
gm_lb47:;
                            }
gm_lb40:;
                            goto gm_lb11;
                        }
gm_lb39:;
                    }
                    {
                        {
                            uint8_t t443 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t444 = (uint8_t)(t443 == ((uint8_t)6ULL));
                            if (!t444) {
                                goto gm_lb52;
                            }
                            uint32_t t445 = GM_LD(uint32_t, (((uint8_t *)t175) + 8));
                            gm_l45 = t445;
                            gm_t88rcucg1b7uk t446 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l46 = t446;
                            void * t447 = gm_l0;
                            gm_t88rcucg1b7uk t448 = gm_l46;
                            void * t449 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t450 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t450)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t450, gm_srckqas99ada0fa, 164, 47);
                            gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)t449) + (uintptr_t)((uint64_t)0ULL) * 16));
                            uint32_t t452 = gm_l45;
                            gm_b16_8 t453 = gm_l15;
                            gm_b0_1 t454 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t447, t448, t451, t452, t453);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb52:;
                        {
                            uint8_t t455 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t456 = (uint8_t)(t455 == ((uint8_t)7ULL));
                            if (!t456) {
                                goto gm_lb53;
                            }
                            uint32_t t457 = GM_LD(uint32_t, (((uint8_t *)t175) + 8));
                            gm_l47 = t457;
                            gm_b16_8 t458 = gm_l15;
                            void * t459 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t460 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t460)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t460, gm_srckqas99ada0fa, 166, 25);
                            void * t461 = GM_LD(void *, (((uint8_t *)t459) + (uintptr_t)((uint64_t)0ULL) * 16));
                            uint64_t t462 = GM_LD(uint64_t, ((((uint8_t *)t459) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                            uint64_t t463 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t464 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t465 = (uintptr_t)t463;
                            if (t464 > t465 || t465 > (uintptr_t)t462) gm_trap_slice(t464, t465, t462, gm_srckqas99ada0fa, 166, 25);
                            uint64_t t466 = ((uintptr_t)t463) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t467;
                            GM_ST(void *, (uint8_t *)&t467, (void *)(((uint8_t *)t461 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t467 + 8, (uintptr_t)(t466));
                            gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
                            gm_f_copy_from_f7j6p05(t458, t468);
                            void * t469 = gm_l0;
                            void * t470 = gm_l4;
                            gm_t88rcucg1b7uk t471 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t470) + 128));
                            uint32_t t472 = gm_l47;
                            gm_tplpbqf3duvn3 t473 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t469, t471, t472);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            gm_l48 = t473;
                            void * t474 = gm_l0;
                            void * t475 = gm_l4;
                            gm_t88rcucg1b7uk t476 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t475) + 128));
                            gm_tkn42u7na3t81 t477 = gm_f_bits_struct_ty_22ue20s(t474, GM_CONV(gm_csguankko4lmhd, t476));
                            gm_l114 = t477;
                            uint8_t t478 = (uint8_t)((((uint8_t *)&gm_l114))[4] != 0);
                            if (t478) {
                                gm_t88rcucg1b7uk t479 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l114));
                                gm_l49 = t479;
                            } else {
                                void * t480 = gm_l4;
                                gm_t88rcucg1b7uk t481 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t480) + 128));
                                gm_l49 = t481;
                            }
                            void * t482 = gm_l0;
                            gm_t88rcucg1b7uk t483 = gm_l49;
                            uint64_t t484 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t482, t483);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint64_t t485 = gm_f_us_cuv01fp(t484);
                            gm_l50 = t485;
                            void * t486 = gm_l0;
                            void * t487 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t488 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t489 = gm_l50;
                            uint64_t t490 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t491 = (uintptr_t)t489;
                            if (t490 > t491 || t491 > (uintptr_t)t488) gm_trap_slice(t490, t491, t488, gm_srckqas99ada0fa, 170, 33);
                            uint64_t t492 = ((uintptr_t)t489) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t493;
                            GM_ST(void *, (uint8_t *)&t493, (void *)(((uint8_t *)t487 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t493 + 8, (uintptr_t)(t492));
                            gm_b16_8 t494 = GM_LD(gm_b16_8, ((uint8_t *)&t493));
                            unsigned __int128 t495 = gm_f_get_uint_bq8c4b7(t486, t494);
                            gm_l51 = t495;
                            void * t496 = gm_l0;
                            gm_t88rcucg1b7uk t497 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l48) + 4));
                            uint32_t t498 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t496, t497);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            gm_l52 = t498;
                            uint32_t t499 = gm_l52;
                            uint8_t t500 = (t499 >= ((uint32_t)128ULL));
                            if (t500) {
                                unsigned __int128 t501 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                                gm_l115 = t501;
                            } else {
                                uint32_t t502 = gm_l52;
                                unsigned __int128 t503 = (unsigned __int128)t502;
                                unsigned __int128 n504 = (unsigned __int128)t503;
                                if (n504 >= 128) gm_trap_shift((uint64_t)n504, "u128", gm_srckqas99ada0fa, 172, 55);
                                unsigned __int128 r505 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n504);
                                bool o506 = (unsigned __int128)(r505 >> (int)n504) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                                if (o506) gm_trap("integer overflow in `<<`", gm_srckqas99ada0fa, 172, 55);
                                unsigned __int128 t507 = r505;
                                unsigned __int128 r508; bool o509 = __builtin_sub_overflow(t507, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r508);
                                if (o509) gm_trap("integer overflow in `-`", gm_srckqas99ada0fa, 172, 54);
                                unsigned __int128 t510 = r508;
                                gm_l115 = t510;
                            }
                            unsigned __int128 t511 = gm_l115;
                            uint32_t t512 = GM_LD(uint32_t, ((uint8_t *)&gm_l48));
                            unsigned __int128 t513 = (unsigned __int128)t512;
                            unsigned __int128 n514 = (unsigned __int128)t513;
                            if (n514 >= 128) gm_trap_shift((uint64_t)n514, "u128", gm_srckqas99ada0fa, 172, 21);
                            unsigned __int128 r515 = (unsigned __int128)((unsigned __int128)t511 << (int)n514);
                            bool o516 = (unsigned __int128)(r515 >> (int)n514) != t511;
                            if (o516) gm_trap("integer overflow in `<<`", gm_srckqas99ada0fa, 172, 21);
                            unsigned __int128 t517 = r515;
                            gm_l53 = t517;
                            void * t518 = gm_l0;
                            gm_t88rcucg1b7uk t519 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l48) + 4));
                            void * t520 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t521 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t521)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t521, gm_srckqas99ada0fa, 173, 38);
                            gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)t520) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t523 = GM_LD(gm_b16_8, ((uint8_t *)&t522));
                            unsigned __int128 t524 = gm_f_bits_raw_t6dqs65((void *)NULL, cx, NULL, t518, t519, t523);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            gm_l54 = t524;
                            void * t525 = gm_l0;
                            void * t526 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t527 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t528 = gm_l50;
                            uint64_t t529 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t530 = (uintptr_t)t528;
                            if (t529 > t530 || t530 > (uintptr_t)t527) gm_trap_slice(t529, t530, t527, gm_srckqas99ada0fa, 174, 24);
                            uint64_t t531 = ((uintptr_t)t528) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t532;
                            GM_ST(void *, (uint8_t *)&t532, (void *)(((uint8_t *)t526 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t532 + 8, (uintptr_t)(t531));
                            unsigned __int128 t533 = gm_l51;
                            unsigned __int128 t534 = gm_l53;
                            unsigned __int128 t535 = (unsigned __int128)~t534;
                            unsigned __int128 t536 = (unsigned __int128)(t533 & t535);
                            unsigned __int128 t537 = gm_l54;
                            uint32_t t538 = GM_LD(uint32_t, ((uint8_t *)&gm_l48));
                            unsigned __int128 t539 = (unsigned __int128)t538;
                            unsigned __int128 n540 = (unsigned __int128)t539;
                            if (n540 >= 128) gm_trap_shift((uint64_t)n540, "u128", gm_srckqas99ada0fa, 174, 53);
                            unsigned __int128 r541 = (unsigned __int128)((unsigned __int128)t537 << (int)n540);
                            bool o542 = (unsigned __int128)(r541 >> (int)n540) != t537;
                            if (o542) gm_trap("integer overflow in `<<`", gm_srckqas99ada0fa, 174, 53);
                            unsigned __int128 t543 = r541;
                            unsigned __int128 t544 = gm_l53;
                            unsigned __int128 t545 = (unsigned __int128)(t543 & t544);
                            unsigned __int128 t546 = (unsigned __int128)(t536 | t545);
                            gm_f_put_uint_cddin99(t525, t532, t546);
                            goto gm_lb11;
                        }
gm_lb53:;
                    }
                    {
                        {
                            uint8_t t547 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t548 = (uint8_t)(t547 == ((uint8_t)8ULL));
                            if (!t548) {
                                goto gm_lb54;
                            }
                            uint64_t t549 = GM_LD(uint64_t, (((uint8_t *)t175) + 8));
                            gm_l55 = t549;
                            uint64_t t550 = gm_l55;
                            uint8_t t551 = (t550 > ((uint64_t)0ULL));
                            if (t551) {
                                uint64_t t552 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t553 = gm_l55;
                                uint64_t t554 = gm_f_us_cuv01fp(t553);
                                if (t554 == 0) gm_trap("division by zero", gm_srckqas99ada0fa, 177, 18);
                                uint64_t t555 = (uint64_t)(t552 / t554);
                                gm_l56 = t555;
                                uint64_t t556 = gm_l56;
                                uint8_t t557 = (t556 > ((uint64_t)0ULL));
                                if (t557) {
                                    gm_l57 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t558 = gm_l57;
                                            uint64_t t559 = gm_l56;
                                            uint64_t r560; bool o561 = __builtin_add_overflow(t558, t559, &r560);
                                            if (o561) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 180, 23);
                                            uint64_t t562 = r560;
                                            uint64_t t563 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint8_t t564 = (t562 <= t563);
                                            if (!t564) {
                                                goto gm_lb55;
                                            }
                                            {
                                                void * t565 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                uint64_t t566 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                uint64_t t567 = gm_l57;
                                                uint64_t t568 = gm_l57;
                                                uint64_t t569 = gm_l56;
                                                uint64_t r570; bool o571 = __builtin_add_overflow(t568, t569, &r570);
                                                if (o571) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 181, 26);
                                                uint64_t t572 = r570;
                                                uint64_t t573 = (uintptr_t)t567;
                                                uint64_t t574 = (uintptr_t)t572;
                                                if (t573 > t574 || t574 > (uintptr_t)t566) gm_trap_slice(t573, t574, t566, gm_srckqas99ada0fa, 181, 21);
                                                uint64_t t575 = ((uintptr_t)t572) - (uintptr_t)t567;
                                                gm_b16_8 t576;
                                                GM_ST(void *, (uint8_t *)&t576, (void *)(((uint8_t *)t565 + (uintptr_t)t567 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t576 + 8, (uintptr_t)(t575));
                                                void * t577 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                uint64_t t578 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t578)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t578, gm_srckqas99ada0fa, 181, 43);
                                                void * t579 = GM_LD(void *, (((uint8_t *)t577) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                uint64_t t580 = GM_LD(uint64_t, ((((uint8_t *)t577) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                uint64_t t581 = gm_l56;
                                                uint64_t t582 = (uintptr_t)((uint64_t)0ULL);
                                                uint64_t t583 = (uintptr_t)t581;
                                                if (t582 > t583 || t583 > (uintptr_t)t580) gm_trap_slice(t582, t583, t580, gm_srckqas99ada0fa, 181, 43);
                                                uint64_t t584 = ((uintptr_t)t581) - (uintptr_t)((uint64_t)0ULL);
                                                gm_b16_8 t585;
                                                GM_ST(void *, (uint8_t *)&t585, (void *)(((uint8_t *)t579 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t585 + 8, (uintptr_t)(t584));
                                                gm_b16_8 t586 = GM_LD(gm_b16_8, ((uint8_t *)&t585));
                                                gm_f_copy_from_f7j6p05(t576, t586);
                                                uint64_t t587 = gm_l57;
                                                uint64_t t588 = gm_l56;
                                                uint64_t r589; bool o590 = __builtin_add_overflow(t587, t588, &r589);
                                                if (o590) gm_trap("integer overflow in `+`", gm_srckqas99ada0fa, 182, 21);
                                                uint64_t t591 = r589;
                                                gm_l57 = t591;
                                            }
gm_lb57:;
                                        }
                                    }
gm_lb55:;
                                }
                            }
                            goto gm_lb11;
                        }
gm_lb54:;
                        {
                            uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t593 = (uint8_t)(t592 == ((uint8_t)9ULL));
                            if (!t593) {
                                goto gm_lb58;
                            }
                            uint8_t t594 = GM_LD(uint8_t, (((uint8_t *)t175) + 8));
                            gm_l58 = t594;
                            gm_b16_8 t595 = gm_l15;
                            uint8_t t596 = gm_l58;
                            gm_f_fill_abmf5g5(t595, t596);
                            goto gm_lb11;
                        }
gm_lb58:;
                        {
                            uint8_t t597 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t598 = (uint8_t)(t597 == ((uint8_t)10ULL));
                            if (!t598) {
                                goto gm_lb59;
                            }
                            gm_b16_8 t599 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 8));
                            gm_l59 = t599;
                            uint8_t t600 = GM_LD(uint8_t, ((((uint8_t *)t175) + 8) + 16));
                            gm_l60 = t600;
                            void * t601 = gm_l0;
                            gm_tl874t2atnlh3 t602 = gm_l16;
                            gm_b16_8 t603 = gm_l59;
                            uint8_t t604 = gm_l60;
                            gm_f_string_literal_337m6cd((void *)&gm_l116, (void *)NULL, cx, NULL, t601, t602, t603, t604);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t605 = (uint8_t)!((((uint8_t *)&gm_l116))[0] != 0);
                            if (t605) {
                            } else {
                                gm_t76123gfet73g t606 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l116) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t606);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            uint64_t t607 = GM_LD(uint64_t, (((uint8_t *)&gm_l116) + 8));
                            gm_l61 = t607;
                            uint8_t t608 = gm_l60;
                            if (t608) {
                                void * t609 = gm_l0;
                                gm_b16_8 t610 = gm_l15;
                                uint64_t t611 = gm_l61;
                                gm_f_put_ptr_188p3mm(t609, t610, t611);
                            } else {
                                void * t612 = gm_l0;
                                gm_b16_8 t613 = gm_l15;
                                uint64_t t614 = gm_l61;
                                uint64_t t615 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                                uint64_t t616 = (uint64_t)t615;
                                gm_f_put_slice_ko14loi(t612, t613, t614, t616);
                            }
                            goto gm_lb11;
                        }
gm_lb59:;
                    }
                    {
                        {
                            uint8_t t617 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t618 = (uint8_t)(t617 == ((uint8_t)11ULL));
                            if (!t618) {
                                goto gm_lb60;
                            }
                            gm_t9qkh0rsooc8d t619 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t175) + 8));
                            gm_l62 = t619;
                            {
                                {
                                    uint8_t t620 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t621 = (uint8_t)(t620 == ((uint8_t)0ULL));
                                    if (!t621) {
                                        goto gm_lb62;
                                    }
                                    gm_b16_8 t622 = gm_l15;
                                    gm_f_fill_abmf5g5(t622, ((uint8_t)0ULL));
                                    goto gm_lb61;
                                }
gm_lb62:;
                                {
                                    uint8_t t623 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t624 = (uint8_t)(t623 == ((uint8_t)1ULL));
                                    if (!t624) {
                                        goto gm_lb63;
                                    }
                                    void * t625 = gm_l0;
                                    void * t626 = gm_l4;
                                    gm_t88rcucg1b7uk t627 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t626) + 128));
                                    gm_b16_8 t628 = gm_l15;
                                    void * t629 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t630 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t630)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t630, gm_srckqas99ada0fa, 193, 43);
                                    gm_b16_8 t631 = GM_LD(gm_b16_8, (((uint8_t *)t629) + (uintptr_t)((uint64_t)0ULL) * 16));
                                    gm_b16_8 t632 = GM_LD(gm_b16_8, ((uint8_t *)&t631));
                                    gm_b0_1 t633 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t625, t627, t628, t632);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    goto gm_lb61;
                                }
gm_lb63:;
                                {
                                    uint8_t t634 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t635 = (uint8_t)(t634 == ((uint8_t)2ULL));
                                    if (!t635) {
                                        goto gm_lb64;
                                    }
                                    void * t636 = gm_l0;
                                    void * t637 = gm_l4;
                                    gm_t88rcucg1b7uk t638 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t637) + 128));
                                    gm_b16_8 t639 = gm_l15;
                                    uint64_t t640 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint8_t t641 = (t640 > ((uint64_t)0ULL));
                                    if (t641) {
                                        void * t642 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                        uint64_t t643 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        gm_b16_8 t644 = GM_LD(gm_b16_8, (((uint8_t *)t642) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_b16_8 t645 = GM_LD(gm_b16_8, ((uint8_t *)&t644));
                                        gm_l117 = t645;
                                    } else {
                                        gm_b16_8 t646 = gm_f_empty_slice_ijtp9lo();
                                        gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)&t646));
                                        gm_l117 = t647;
                                    }
                                    gm_b16_8 t648 = gm_l117;
                                    gm_b0_1 t649 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t636, t638, t639, ((uint8_t)0), t648);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    goto gm_lb61;
                                }
gm_lb64:;
                                {
                                    uint8_t t650 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                                    uint8_t t651 = (uint8_t)(t650 == ((uint8_t)3ULL));
                                    if (!t651) {
                                        goto gm_lb65;
                                    }
                                    void * t652 = gm_l0;
                                    void * t653 = gm_l4;
                                    gm_t88rcucg1b7uk t654 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t653) + 128));
                                    gm_b16_8 t655 = gm_l15;
                                    void * t656 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t657 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t657)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t657, gm_srckqas99ada0fa, 195, 47);
                                    gm_b16_8 t658 = GM_LD(gm_b16_8, (((uint8_t *)t656) + (uintptr_t)((uint64_t)0ULL) * 16));
                                    gm_b16_8 t659 = GM_LD(gm_b16_8, ((uint8_t *)&t658));
                                    gm_b0_1 t660 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t652, t654, t655, ((uint8_t)1), t659);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    goto gm_lb61;
                                }
gm_lb65:;
                                gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 191, 24);
                            }
gm_lb61:;
                            goto gm_lb11;
                        }
gm_lb60:;
                        {
                            uint8_t t661 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t662 = (uint8_t)(t661 == ((uint8_t)12ULL));
                            if (!t662) {
                                goto gm_lb66;
                            }
                            uint32_t t663 = GM_LD(uint32_t, (((uint8_t *)t175) + 8));
                            gm_l63 = t663;
                            void * t664 = gm_l0;
                            void * t665 = gm_l4;
                            gm_t88rcucg1b7uk t666 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t665) + 128));
                            gm_b16_8 t667 = gm_l15;
                            uint32_t t668 = gm_l63;
                            uint64_t t669 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t670 = (t669 > ((uint64_t)0ULL));
                            if (t670) {
                                void * t671 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t672 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                gm_b16_8 t673 = GM_LD(gm_b16_8, (((uint8_t *)t671) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t674 = GM_LD(gm_b16_8, ((uint8_t *)&t673));
                                gm_l118 = t674;
                            } else {
                                gm_b16_8 t675 = gm_f_empty_slice_ijtp9lo();
                                gm_b16_8 t676 = GM_LD(gm_b16_8, ((uint8_t *)&t675));
                                gm_l118 = t676;
                            }
                            gm_b16_8 t677 = gm_l118;
                            gm_b0_1 t678 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t664, t666, t667, t668, t677);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb66:;
                    }
                    {
                        {
                            uint8_t t679 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t680 = (uint8_t)(t679 == ((uint8_t)13ULL));
                            if (!t680) {
                                goto gm_lb67;
                            }
                            uint8_t t681 = GM_LD(uint8_t, (((uint8_t *)t175) + 8));
                            gm_l64 = t681;
                            uint64_t t682 = GM_LD(uint64_t, ((((uint8_t *)t175) + 8) + 8));
                            gm_l65 = t682;
                            void * t683 = gm_l0;
                            void * t684 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t685 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t685)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t685, gm_srckqas99ada0fa, 199, 32);
                            gm_b16_8 t686 = GM_LD(gm_b16_8, (((uint8_t *)t684) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t687 = GM_LD(gm_b16_8, ((uint8_t *)&t686));
                            uint64_t t688 = gm_f_gen_index_o6g9ar2(t683, t687);
                            gm_l66 = t688;
                            void * t689 = gm_l0;
                            void * t690 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t691 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t691)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t691, gm_srckqas99ada0fa, 200, 32);
                            gm_b16_8 t692 = GM_LD(gm_b16_8, (((uint8_t *)t690) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t693 = GM_LD(gm_b16_8, ((uint8_t *)&t692));
                            uint64_t t694 = gm_f_gen_index_o6g9ar2(t689, t693);
                            gm_l67 = t694;
                            uint8_t t695 = gm_l64;
                            if (t695) {
                                uint64_t t696 = gm_l67;
                                uint64_t r697; bool o698 = __builtin_add_overflow(t696, ((uint64_t)1ULL), &r697);
                                uint64_t t699 = o698 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r697;
                                gm_l67 = t699;
                            }
                            void * t700 = gm_l0;
                            gm_b16_8 t701 = gm_l15;
                            void * t702 = gm_l0;
                            void * t703 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t704 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t704)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t704, gm_srckqas99ada0fa, 202, 37);
                            gm_b16_8 t705 = GM_LD(gm_b16_8, (((uint8_t *)t703) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t706 = GM_LD(gm_b16_8, ((uint8_t *)&t705));
                            uint64_t t707 = gm_f_ptr_of_b77b4c9(t702, t706);
                            uint64_t t708 = gm_l66;
                            uint64_t t709 = gm_l65;
                            uint64_t r710; bool o711 = __builtin_mul_overflow(t708, t709, &r710);
                            uint64_t t712 = r710;
                            uint64_t r713; bool o714 = __builtin_add_overflow(t707, t712, &r713);
                            uint64_t t715 = r713;
                            uint64_t t716 = gm_l67;
                            uint64_t t717 = gm_l66;
                            uint64_t r718; bool o719 = __builtin_sub_overflow(t716, t717, &r718);
                            uint64_t t720 = r718;
                            gm_f_put_slice_ko14loi(t700, t701, t715, t720);
                            goto gm_lb11;
                        }
gm_lb67:;
                        {
                            uint8_t t721 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t722 = (uint8_t)(t721 == ((uint8_t)14ULL));
                            if (!t722) {
                                goto gm_lb68;
                            }
                            uint8_t t723 = GM_LD(uint8_t, (((uint8_t *)t175) + 8));
                            gm_l68 = t723;
                            uint8_t t724 = GM_LD(uint8_t, ((((uint8_t *)t175) + 8) + 1));
                            gm_l69 = t724;
                            void * t725 = gm_l0;
                            void * t726 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t727 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t727)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t727, gm_srckqas99ada0fa, 205, 32);
                            gm_b16_8 t728 = GM_LD(gm_b16_8, (((uint8_t *)t726) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t729 = GM_LD(gm_b16_8, ((uint8_t *)&t728));
                            uint64_t t730 = gm_f_gen_index_o6g9ar2(t725, t729);
                            gm_l70 = t730;
                            void * t731 = gm_l0;
                            void * t732 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t733 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t733)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t733, gm_srckqas99ada0fa, 206, 32);
                            gm_b16_8 t734 = GM_LD(gm_b16_8, (((uint8_t *)t732) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t735 = GM_LD(gm_b16_8, ((uint8_t *)&t734));
                            uint64_t t736 = gm_f_gen_index_o6g9ar2(t731, t735);
                            gm_l71 = t736;
                            uint8_t t737 = gm_l68;
                            if (t737) {
                                uint64_t t738 = gm_l71;
                                uint64_t r739; bool o740 = __builtin_add_overflow(t738, ((uint64_t)1ULL), &r739);
                                uint64_t t741 = o740 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r739;
                                gm_l71 = t741;
                            }
                            uint8_t t742 = gm_l69;
                            if (t742) {
                                void * t743 = gm_l0;
                                void * t744 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t745 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t745)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t745, gm_srckqas99ada0fa, 208, 51);
                                gm_b16_8 t746 = GM_LD(gm_b16_8, (((uint8_t *)t744) + (uintptr_t)((uint64_t)2ULL) * 16));
                                gm_b16_8 t747 = GM_LD(gm_b16_8, ((uint8_t *)&t746));
                                uint64_t t748 = gm_f_gen_index_o6g9ar2(t743, t747);
                                gm_tf5srh4mp4b3i t749;
                                GM_ST(uint8_t, (uint8_t *)&t749 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t749 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t749 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t749), t748);
                                ((uint8_t *)&t749)[8] = 1;
                                gm_l72 = t749;
                            } else {
                                gm_tf5srh4mp4b3i t750;
                                memset(&t750, 0, sizeof t750);
                                gm_l72 = t750;
                            }
                            uint64_t t751 = gm_l70;
                            uint64_t t752 = gm_l71;
                            uint8_t t753 = (t751 > t752);
                            gm_l73 = t753;
                            {
                                {
                                    uint8_t t754 = (uint8_t)((((uint8_t *)&gm_l72))[8] != 0);
                                    if (!t754) {
                                        goto gm_lb70;
                                    }
                                    uint64_t t755 = GM_LD(uint64_t, ((uint8_t *)&gm_l72));
                                    gm_l74 = t755;
                                    uint64_t t756 = gm_l71;
                                    uint64_t t757 = gm_l74;
                                    uint8_t t758 = (t756 > t757);
                                    if (!t758) {
                                        goto gm_lb70;
                                    }
                                    gm_l73 = ((uint8_t)1);
                                    goto gm_lb69;
                                }
gm_lb70:;
                            }
gm_lb69:;
                            uint8_t t759 = gm_l73;
                            if (t759) {
                                {
                                    {
                                        uint8_t t760 = (uint8_t)((((uint8_t *)&gm_l72))[8] != 0);
                                        if (!t760) {
                                            goto gm_lb72;
                                        }
                                        uint64_t t761 = GM_LD(uint64_t, ((uint8_t *)&gm_l72));
                                        gm_l75 = t761;
                                        uint64_t t762 = gm_l75;
                                        gm_t4ce0jd3tdro4 t763;
                                        GM_ST(uint64_t, ((uint8_t *)&t763 + 0), t762);
                                        gm_f_format_1amnh72((void *)&gm_l119, (void *)NULL, cx, NULL, t763);
                                        if (gm_unw.kind) {
                                            goto gm_ld87_1;
                                        }
                                        uint8_t t764 = (uint8_t)!((((uint8_t *)&gm_l119))[0] != 0);
                                        if (!t764) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l119) + 8, gm_srckqas99ada0fa, 212, 39);
                                        gm_b16_8 t765 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l119) + 8));
                                        gm_b16_8 t766 = GM_LD(gm_b16_8, ((uint8_t *)&t765));
                                        gm_l76 = t766;
                                        goto gm_lb71;
                                    }
gm_lb72:;
                                    gm_b16_8 t767;
                                    GM_ST(void *, (uint8_t *)&t767, (void *)(gm_sfqk3rro4g7t0));
                                    GM_ST(uintptr_t, (uint8_t *)&t767 + 8, (uintptr_t)(1));
                                    gm_l76 = t767;
                                }
gm_lb71:;
                                {
                                    void * t768 = gm_l0;
                                    gm_tl874t2atnlh3 t769 = gm_l16;
                                    uint64_t t770 = gm_l70;
                                    uint64_t t771 = gm_l71;
                                    gm_b16_8 t772 = gm_l76;
                                    gm_tbccvikg6b96e t773;
                                    GM_ST(uint64_t, ((uint8_t *)&t773 + 0), t770);
                                    GM_ST(uint64_t, ((uint8_t *)&t773 + 8), t771);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t773 + 16), t772);
                                    gm_f_format_3pa9rer((void *)&gm_l120, (void *)NULL, cx, NULL, &t773);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    uint8_t t774 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
                                    if (!t774) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 8, gm_srckqas99ada0fa, 213, 38);
                                    gm_b16_8 t775 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l120) + 8));
                                    gm_b16_8 t776 = GM_LD(gm_b16_8, ((uint8_t *)&t775));
                                    gm_t76123gfet73g t777;
                                    gm_f_trap_qh84l9d((void *)&t777, (void *)NULL, cx, NULL, t768, t769, t776);
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                    ((uint8_t *)&F.ret)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t777);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                    goto gm_ld87_1;
                                }
                            }
                            goto gm_lb11;
                        }
gm_lb68:;
                    }
                    {
                        {
                            uint8_t t778 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t779 = (uint8_t)(t778 == ((uint8_t)15ULL));
                            if (!t779) {
                                goto gm_lb73;
                            }
                            void * t780 = gm_l0;
                            void * t781 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t782 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t782)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t782, gm_srckqas99ada0fa, 217, 31);
                            gm_b16_8 t783 = GM_LD(gm_b16_8, (((uint8_t *)t781) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t784 = GM_LD(gm_b16_8, ((uint8_t *)&t783));
                            uint64_t t785 = gm_f_gen_index_o6g9ar2(t780, t784);
                            gm_l77 = t785;
                            void * t786 = gm_l0;
                            void * t787 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t788 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t788)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t788, gm_srckqas99ada0fa, 218, 33);
                            gm_b16_8 t789 = GM_LD(gm_b16_8, (((uint8_t *)t787) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t790 = GM_LD(gm_b16_8, ((uint8_t *)&t789));
                            uint64_t t791 = gm_f_gen_index_o6g9ar2(t786, t790);
                            gm_l78 = t791;
                            uint64_t t792 = gm_l77;
                            uint64_t t793 = gm_l78;
                            uint8_t t794 = (t792 >= t793);
                            if (t794) {
                                void * t795 = gm_l0;
                                gm_tl874t2atnlh3 t796 = gm_l16;
                                uint64_t t797 = gm_l77;
                                uint64_t t798 = gm_l78;
                                gm_t2lqu8jsj511d t799;
                                GM_ST(uint64_t, ((uint8_t *)&t799 + 0), t797);
                                GM_ST(uint64_t, ((uint8_t *)&t799 + 8), t798);
                                gm_f_format_2hd15t9((void *)&gm_l121, (void *)NULL, cx, NULL, t799);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                uint8_t t800 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
                                if (!t800) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l121) + 8, gm_srckqas99ada0fa, 219, 48);
                                gm_b16_8 t801 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l121) + 8));
                                gm_b16_8 t802 = GM_LD(gm_b16_8, ((uint8_t *)&t801));
                                gm_t76123gfet73g t803;
                                gm_f_trap_qh84l9d((void *)&t803, (void *)NULL, cx, NULL, t795, t796, t802);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t803);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb73:;
                        {
                            uint8_t t804 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t805 = (uint8_t)(t804 == ((uint8_t)16ULL));
                            if (!t805) {
                                goto gm_lb74;
                            }
                            uint32_t t806 = GM_LD(uint32_t, (((uint8_t *)t175) + 8));
                            gm_l79 = t806;
                            gm_t88rcucg1b7uk t807 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + 8) + 4));
                            gm_l80 = t807;
                            void * t808 = gm_l0;
                            gm_t88rcucg1b7uk t809 = gm_l80;
                            void * t810 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t811 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t811)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t811, gm_srckqas99ada0fa, 222, 37);
                            gm_b16_8 t812 = GM_LD(gm_b16_8, (((uint8_t *)t810) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t813 = GM_LD(gm_b16_8, ((uint8_t *)&t812));
                            gm_t9805qtonbsag t814 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t808, t809, t813);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            gm_l81 = t814;
                            void * t815 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t816 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t816)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t816, gm_srckqas99ada0fa, 223, 13);
                            gm_t9805qtonbsag t817 = gm_l81;
                            uint32_t t818 = gm_l79;
                            gm_t9805qtonbsag t819;
                            GM_ST(uint8_t, (uint8_t *)&t819 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t819 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t819), t818);
                            ((uint8_t *)&t819)[4] = 1;
                            uint8_t t820 = (gm_eqkricceou4u(((uint8_t *)&t817), ((uint8_t *)&t819)));
                            if (t820) {
                                gm_l122 = ((uint8_t)1ULL);
                            } else {
                                gm_l122 = ((uint8_t)0ULL);
                            }
                            uint8_t t821 = gm_l122;
                            GM_ST(uint8_t, (((uint8_t *)t815) + (uintptr_t)((uint64_t)0ULL) * 1), t821);
                            goto gm_lb11;
                        }
gm_lb74:;
                    }
                    {
                        {
                            uint8_t t822 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t823 = (uint8_t)(t822 == ((uint8_t)17ULL));
                            if (!t823) {
                                goto gm_lb75;
                            }
                            {
                                void * t824 = gm_l0;
                                gm_tl874t2atnlh3 t825 = gm_l16;
                                void * t826 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t827 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t827)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t827, gm_srckqas99ada0fa, 226, 42);
                                gm_b16_8 t828 = GM_LD(gm_b16_8, (((uint8_t *)t826) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t829 = GM_LD(gm_b16_8, ((uint8_t *)&t828));
                                gm_f_read_string_ttuk4gr((void *)&gm_l123, (void *)NULL, cx, NULL, t824, t825, t829);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                            }
                            uint8_t t830 = (uint8_t)!((((uint8_t *)&gm_l123))[0] != 0);
                            if (t830) {
                            } else {
                                gm_t76123gfet73g t831 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l123) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t831);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            gm_b16_8 t832 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l123) + 8));
                            gm_l82 = t832;
                            {
                                void * t833 = gm_l0;
                                gm_tl874t2atnlh3 t834 = gm_l16;
                                gm_b16_8 t835 = gm_l82;
                                gm_t76123gfet73g t836;
                                gm_f_trap_qh84l9d((void *)&t836, (void *)NULL, cx, NULL, t833, t834, t835);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t836);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                        }
gm_lb75:;
                        {
                            uint8_t t837 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t838 = (uint8_t)(t837 == ((uint8_t)18ULL));
                            if (!t838) {
                                goto gm_lb76;
                            }
                            gm_b16_8 t839 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 8));
                            gm_l83 = t839;
                            {
                                void * t840 = gm_l0;
                                gm_tl874t2atnlh3 t841 = gm_l16;
                                gm_b16_8 t842 = gm_l83;
                                gm_t76123gfet73g t843;
                                gm_f_trap_qh84l9d((void *)&t843, (void *)NULL, cx, NULL, t840, t841, t842);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t843);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                        }
gm_lb76:;
                        {
                            uint8_t t844 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t845 = (uint8_t)(t844 == ((uint8_t)19ULL));
                            if (!t845) {
                                goto gm_lb77;
                            }
                            gm_b16_8 t846 = GM_LD(gm_b16_8, (((uint8_t *)t175) + 8));
                            gm_l84 = t846;
                            {
                                void * t847 = gm_l0;
                                gm_tl874t2atnlh3 t848 = gm_l16;
                                gm_b16_8 t849 = gm_l84;
                                gm_t76123gfet73g t850;
                                gm_f_unsupported_70vvk1e((void *)&t850, (void *)NULL, cx, NULL, t847, t848, t849);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t850);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                        }
gm_lb77:;
                        {
                            uint8_t t851 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t852 = (uint8_t)(t851 == ((uint8_t)20ULL));
                            if (!t852) {
                                goto gm_lb78;
                            }
                            {
                                memmove(((uint8_t *)&F.l85), (((uint8_t *)t175) + 8), 40);
                            }
                            gm_b16_8 t853 = GM_LD(gm_b16_8, ((((uint8_t *)t175) + 8) + 40));
                            gm_l86 = t853;
                            gm_b16_8 t854 = GM_LD(gm_b16_8, ((((uint8_t *)t175) + 8) + 56));
                            gm_l87 = t854;
                            gm_b16_8 t855 = GM_LD(gm_b16_8, ((((uint8_t *)t175) + 8) + 72));
                            gm_l88 = t855;
                            uint8_t t856 = GM_LD(uint8_t, ((((uint8_t *)t175) + 8) + 88));
                            gm_l89 = t856;
                            {
                                void * t857 = gm_l0;
                                uint32_t t858 = gm_l1;
                                gm_tl874t2atnlh3 t859 = gm_l16;
                                gm_b16_8 t860 = gm_l86;
                                gm_b16_8 t861 = gm_l87;
                                gm_b16_8 t862 = gm_l88;
                                uint8_t t863 = gm_l89;
                                gm_b16_8 t864 = gm_l8;
                                gm_tcgnlqgmuvh2q t865 = GM_LD(gm_tcgnlqgmuvh2q, ((uint8_t *)&F.l85));
                                gm_f_gen_write_1pcgnkv((void *)&gm_l124, (void *)NULL, cx, NULL, t857, t858, t859, &t865, t860, t861, t862, t863, t864);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                            }
                            uint8_t t866 = (uint8_t)!((((uint8_t *)&gm_l124))[0] != 0);
                            if (t866) {
                            } else {
                                gm_t76123gfet73g t867 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l124) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t867);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb78:;
                    }
                    {
                        {
                            uint8_t t868 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t869 = (uint8_t)(t868 == ((uint8_t)21ULL));
                            if (!t869) {
                                goto gm_lb79;
                            }
                            uint32_t t870 = GM_LD(uint32_t, (((uint8_t *)t175) + 8));
                            gm_l90 = t870;
                            void * t871 = gm_l0;
                            gm_b16_8 t872 = gm_l15;
                            void * t873 = gm_l0;
                            uint32_t t874 = gm_l90;
                            uint64_t t875 = gm_f_fn_addr_odbbf6a(t873, t874);
                            gm_f_put_word_tfn443i(t871, t872, ((uint64_t)0ULL), t875);
                            void * t876 = gm_l0;
                            gm_b16_8 t877 = gm_l15;
                            gm_f_put_word_tfn443i(t876, t877, ((uint64_t)1ULL), ((uint64_t)0ULL));
                            void * t878 = gm_l0;
                            gm_b16_8 t879 = gm_l15;
                            uint32_t t880 = gm_l1;
                            uint64_t t881 = (uint64_t)t880;
                            gm_f_put_word_tfn443i(t878, t879, ((uint64_t)2ULL), t881);
                            goto gm_lb11;
                        }
gm_lb79:;
                        {
                            uint8_t t882 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t883 = (uint8_t)(t882 == ((uint8_t)22ULL));
                            if (!t883) {
                                goto gm_lb80;
                            }
                            uint8_t t884 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                            if (t884) {
                                void * t885 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                gm_l91 = t885;
                            } else {
                                void * t886 = gm_l0;
                                gm_tl874t2atnlh3 t887 = gm_l16;
                                gm_b16_8 t888;
                                GM_ST(void *, (uint8_t *)&t888, (void *)(gm_sq8e4jok48g31));
                                GM_ST(uintptr_t, (uint8_t *)&t888 + 8, (uintptr_t)(47));
                                gm_t76123gfet73g t889;
                                gm_f_trap_qh84l9d((void *)&t889, (void *)NULL, cx, NULL, t886, t887, t888);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t889);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            void * t890 = gm_l0;
                            gm_b16_8 t891 = gm_l15;
                            void * t892 = gm_l0;
                            void * t893 = gm_l91;
                            uint32_t t894 = gm_f_frame_handle_k12gn88((void *)NULL, cx, NULL, t892, t893);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint64_t t895 = (uint64_t)t894;
                            gm_f_put_word_tfn443i(t890, t891, ((uint64_t)0ULL), t895);
                            void * t896 = gm_l0;
                            gm_b16_8 t897 = gm_l15;
                            uint32_t t898 = gm_l1;
                            uint64_t t899 = (uint64_t)t898;
                            gm_f_put_word_tfn443i(t896, t897, ((uint64_t)1ULL), t899);
                            goto gm_lb11;
                        }
gm_lb80:;
                        {
                            uint8_t t900 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t901 = (uint8_t)(t900 == ((uint8_t)23ULL));
                            if (!t901) {
                                goto gm_lb81;
                            }
                            gm_t88rcucg1b7uk t902 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t175) + 8));
                            gm_l92 = t902;
                            uint8_t t903 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                            if (t903) {
                                void * t904 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                gm_l93 = t904;
                            } else {
                                void * t905 = gm_l0;
                                gm_tl874t2atnlh3 t906 = gm_l16;
                                gm_b16_8 t907;
                                GM_ST(void *, (uint8_t *)&t907, (void *)(gm_s1vst5t3hdg5s));
                                GM_ST(uintptr_t, (uint8_t *)&t907 + 8, (uintptr_t)(40));
                                gm_t76123gfet73g t908;
                                gm_f_trap_qh84l9d((void *)&t908, (void *)NULL, cx, NULL, t905, t906, t907);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t908);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            void * t909 = gm_l0;
                            void * t910 = gm_l93;
                            gm_tl874t2atnlh3 t911 = gm_l16;
                            void * t912 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t913 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t913)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t913, gm_srckqas99ada0fa, 244, 38);
                            gm_b16_8 t914 = GM_LD(gm_b16_8, (((uint8_t *)t912) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_t88rcucg1b7uk t915 = gm_l92;
                            void * t916 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t917 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t917)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t917, gm_srckqas99ada0fa, 244, 49);
                            gm_b16_8 t918 = GM_LD(gm_b16_8, (((uint8_t *)t916) + (uintptr_t)((uint64_t)1ULL) * 16));
                            void * t919 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t920 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t920)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t920, gm_srckqas99ada0fa, 244, 55);
                            gm_b16_8 t921 = GM_LD(gm_b16_8, (((uint8_t *)t919) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t922 = gm_l15;
                            gm_f_gen_fiber_nhmvpkp((void *)&gm_l125, (void *)NULL, cx, NULL, t909, t910, t911, t914, t915, t918, t921, t922);
                            if (gm_unw.kind) {
                                goto gm_ld87_1;
                            }
                            uint8_t t923 = (uint8_t)!((((uint8_t *)&gm_l125))[0] != 0);
                            if (t923) {
                            } else {
                                gm_t76123gfet73g t924 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l125) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t924);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            goto gm_lb11;
                        }
gm_lb81:;
                    }
                    {
                        {
                            uint8_t t925 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t926 = (uint8_t)(t925 == ((uint8_t)24ULL));
                            if (!t926) {
                                goto gm_lb82;
                            }
                            uint8_t t927 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                            if (t927) {
                                void * t928 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                gm_l94 = t928;
                            } else {
                                void * t929 = gm_l0;
                                gm_tl874t2atnlh3 t930 = gm_l16;
                                gm_b16_8 t931;
                                GM_ST(void *, (uint8_t *)&t931, (void *)(gm_s7pgps43h1eg7));
                                GM_ST(uintptr_t, (uint8_t *)&t931 + 8, (uintptr_t)(40));
                                gm_t76123gfet73g t932;
                                gm_f_trap_qh84l9d((void *)&t932, (void *)NULL, cx, NULL, t929, t930, t931);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t932);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            {
                                void * t933 = gm_l0;
                                gm_tl874t2atnlh3 t934 = gm_l16;
                                void * t935 = gm_l94;
                                void * t936 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t937 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t937)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t937, gm_srckqas99ada0fa, 248, 41);
                                gm_b16_8 t938 = GM_LD(gm_b16_8, (((uint8_t *)t936) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t939 = GM_LD(gm_b16_8, ((uint8_t *)&t938));
                                gm_f_consume_u5eqint((void *)&gm_l126, (void *)NULL, cx, NULL, t933, t934, t935, t939);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                            }
                            uint8_t t940 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                            if (t940) {
                            } else {
                                gm_t76123gfet73g t941 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l126) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t941);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            {
                                memmove(((uint8_t *)&gm_l95), (((uint8_t *)&gm_l126) + 8), 24);
                            }
                            gm_b16_8 t942 = gm_f_empty_slice_ijtp9lo();
                            gm_l96 = t942;
                            void * t943 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t944 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t944)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t944, gm_srckqas99ada0fa, 250, 16);
                            uint64_t t945 = GM_LD(uint64_t, ((((uint8_t *)t943) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                            uint8_t t946 = (t945 > ((uint64_t)0ULL));
                            if (t946) {
                                {
                                    void * t947 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t948 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t948)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t948, gm_srckqas99ada0fa, 251, 34);
                                    uint64_t t949 = GM_LD(uint64_t, ((((uint8_t *)t947) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                                    gm_f_make_2ip78bj((void *)&gm_l127, (void *)NULL, cx, NULL, t949, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf4jqnuehk8)));
                                    if (gm_unw.kind) {
                                        goto gm_ld87_1;
                                    }
                                }
                                uint8_t t950 = (uint8_t)!((((uint8_t *)&gm_l127))[0] != 0);
                                if (!t950) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l127) + 8, gm_srckqas99ada0fa, 251, 25);
                                gm_b16_8 t951 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l127) + 8));
                                gm_l96 = t951;
                                gm_b16_8 t952 = gm_l96;
                                void * t953 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t954 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t954)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t954, gm_srckqas99ada0fa, 252, 33);
                                gm_b16_8 t955 = GM_LD(gm_b16_8, (((uint8_t *)t953) + (uintptr_t)((uint64_t)1ULL) * 16));
                                gm_b16_8 t956 = GM_LD(gm_b16_8, ((uint8_t *)&t955));
                                gm_f_copy_from_f7j6p05(t952, t956);
                            }
                            void * t957 = gm_l0;
                            gm_b16_8 t958 = gm_l96;
                            GM_ST(gm_b16_8, ((((uint8_t *)t957) + 1064) + 144), t958);
                            void * t959 = gm_l0;
                            gm_t2lqu8jsj511d t960 = gm_l5;
                            gm_f_release_jn5kqdi(t959, GM_CONV(gm_csc7lc6jlkqkuq, t960));
                            {
                                uint32_t t961 = GM_LD(uint32_t, ((uint8_t *)&gm_l95));
                                gm_t76123gfet73g t962;
                                GM_ST(uint8_t, (uint8_t *)&t962 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t962 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t962 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t962 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t962 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t962 + 0), ((uint8_t)5ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t962 + 8), t961);
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t962);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                        }
gm_lb82:;
                    }
                    {
                        {
                            uint8_t t963 = GM_LD(uint8_t, (((uint8_t *)t175) + 0));
                            uint8_t t964 = (uint8_t)(t963 == ((uint8_t)25ULL));
                            if (!t964) {
                                goto gm_lb83;
                            }
                            uint8_t t965 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                            if (t965) {
                                void * t966 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                gm_l97 = t966;
                            } else {
                                void * t967 = gm_l0;
                                gm_tl874t2atnlh3 t968 = gm_l16;
                                gm_b16_8 t969;
                                GM_ST(void *, (uint8_t *)&t969, (void *)(gm_si7q8ca92ce9b));
                                GM_ST(uintptr_t, (uint8_t *)&t969 + 8, (uintptr_t)(40));
                                gm_t76123gfet73g t970;
                                gm_f_trap_qh84l9d((void *)&t970, (void *)NULL, cx, NULL, t967, t968, t969);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t970);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            {
                                void * t971 = gm_l0;
                                gm_tl874t2atnlh3 t972 = gm_l16;
                                void * t973 = gm_l97;
                                void * t974 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t975 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t975)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t975, gm_srckqas99ada0fa, 260, 41);
                                gm_b16_8 t976 = GM_LD(gm_b16_8, (((uint8_t *)t974) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t977 = GM_LD(gm_b16_8, ((uint8_t *)&t976));
                                gm_f_consume_u5eqint((void *)&gm_l128, (void *)NULL, cx, NULL, t971, t972, t973, t977);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                            }
                            uint8_t t978 = (uint8_t)!((((uint8_t *)&gm_l128))[0] != 0);
                            if (t978) {
                            } else {
                                gm_t76123gfet73g t979 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l128) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t979);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            {
                                memmove(((uint8_t *)&gm_l98), (((uint8_t *)&gm_l128) + 8), 24);
                            }
                            void * t980 = gm_l0;
                            gm_t2lqu8jsj511d t981 = gm_l5;
                            gm_f_release_jn5kqdi(t980, GM_CONV(gm_csc7lc6jlkqkuq, t981));
                            {
                                void * t982 = gm_l0;
                                gm_tl874t2atnlh3 t983 = gm_l16;
                                gm_tn68el8d14vku t984 = GM_LD(gm_tn68el8d14vku, (((uint8_t *)&gm_l98) + 8));
                                uint32_t t985 = GM_LD(uint32_t, ((uint8_t *)&gm_l98));
                                gm_f_cancel_fiber_n4k6buo((void *)&gm_l129, (void *)NULL, cx, NULL, t982, t983, t984, t985);
                                if (gm_unw.kind) {
                                    goto gm_ld87_1;
                                }
                            }
                            uint8_t t986 = (uint8_t)!((((uint8_t *)&gm_l129))[0] != 0);
                            if (t986) {
                            } else {
                                gm_t76123gfet73g t987 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l129) + 8));
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                ((uint8_t *)&F.ret)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t987);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                            {
                                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.ret + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                                ((uint8_t *)&F.ret)[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&F.ret + 8), GM_ZERO(gm_b0_1));
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                goto gm_ld87_1;
                            }
                        }
gm_lb83:;
                        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 118, 5);
                    }
                }
gm_lb11:;
            }
            {
                void * t988 = gm_l0;
                gm_t2lqu8jsj511d t989 = gm_l5;
                gm_f_release_jn5kqdi(t988, GM_CONV(gm_csc7lc6jlkqkuq, t989));
                {
                    {
                        void * t990 = gm_l4;
                        uint8_t t991 = GM_LD(uint8_t, ((((uint8_t *)t990) + 120) + 0));
                        uint8_t t992 = (uint8_t)(t991 == ((uint8_t)1ULL));
                        if (!t992) {
                            goto gm_lb85;
                        }
                        uint32_t t993 = GM_LD(uint32_t, ((((uint8_t *)t990) + 120) + 4));
                        gm_l99 = t993;
                        void * t994 = gm_l3;
                        uint32_t t995 = gm_l99;
                        uint64_t t996 = (uint64_t)t995;
                        void * t997 = gm_l0;
                        gm_b16_8 t998 = gm_l15;
                        gm_b16_8 t999 = GM_LD(gm_b16_8, ((uint8_t *)&t998));
                        uint64_t t1000 = gm_f_rd_word_8flcq6o(t997, t999);
                        GM_ST(uint64_t, (((uint8_t *)t994) + (uintptr_t)t996 * 8), t1000);
                        goto gm_lb84;
                    }
gm_lb85:;
                }
gm_lb84:;
            }
        }
        {
            uint64_t t1001 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t1002 = (t1001 > ((uint64_t)0ULL));
            if (t1002) {
                gm_b16_8 t1003 = gm_l7;
                gm_b16_8 t1004 = GM_LD(gm_b16_8, ((uint8_t *)&t1003));
                gm_b0_1 t1005 = gm_f_free_ggsg0cj((void *)NULL, cx, NULL, t1004, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knjegli7s8j)));
            }
        }
        goto gm_ls1006;
gm_ld87_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            uint64_t t1007 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t1008 = (t1007 > ((uint64_t)0ULL));
            if (t1008) {
                gm_b16_8 t1009 = gm_l7;
                gm_b16_8 t1010 = GM_LD(gm_b16_8, ((uint8_t *)&t1009));
                gm_b0_1 t1011 = gm_f_free_ggsg0cj((void *)NULL, cx, NULL, t1010, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knjegli7s8j)));
            }
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls1006:;
gm_lb1:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
extern __attribute__((aligned(4))) void gm_f_vm_sub_uskpr78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_vm_with_on_t2rnhma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3) {
    struct gm_fr_vm_with_on_t2rnhma F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* regs */
    void * gm_l3; /* x */
    void * gm_l4; /* fr */
    gm_b16_8 gm_l5; /* stack */
    gm_tn68el8d14vku gm_l6; /* key */
    uint32_t gm_l7; /* first */
    gm_tcfa6l7reoosn gm_l8; /* r */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_t76123gfet73g gm_l13; /* f */
    uint64_t gm_l14; /* frame */
    uint32_t gm_l15; /* label */
    GM_STACK_CHECK(&F, 1444u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tcfa6l7reoosn gm_l16;
    uint8_t gm_l17;
    void * gm_l18;
    gm_tftb1odc739t5 gm_l19;
    void * t12 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l4 = t12;
    void * t13 = gm_l2;
    void * t14 = gm_l3;
    gm_t9rfq1ocel0om t15 = GM_LD(gm_t9rfq1ocel0om, ((uint8_t *)t14));
    gm_b16_8 t16 = gm_f_gen_bytes_bvo3jme(t13, t15);
    gm_l5 = t16;
    {
        void * t17 = gm_l0;
        void * t18 = gm_l3;
        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 28));
        gm_b16_8 t20 = gm_l5;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_f_check_stack_eei454t((void *)&gm_l16, (void *)NULL, cx, NULL, t17, t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (t22) {
    } else {
        gm_t76123gfet73g t23 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l16) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t23);
        goto gm_fdone;
    }
    void * t24 = gm_l4;
    uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)t24));
    void * t26 = gm_l3;
    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 24));
    gm_tn68el8d14vku t28;
    GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t25);
    GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
    gm_l6 = t28;
    void * t29 = gm_l0;
    gm_tn68el8d14vku t30 = gm_l6;
    void * t31 = gm_f_with_rec_2eah8tp(t29, GM_CONV(gm_csff7attl2ehc5, t30));
    gm_l18 = t31;
    {
        {
            uint8_t t32 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l18)) != 0);
            if (t32) {
                goto gm_lb2;
            }
            gm_l17 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l17 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 380, 8);
    }
gm_lb1:;
    uint8_t t33 = gm_l17;
    if (t33) {
        void * t34 = gm_l0;
        void * t35 = gm_l3;
        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 28));
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s3mvfcnrk9f21));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(47));
        gm_t76123gfet73g t38;
        gm_f_trap_qh84l9d((void *)&t38, (void *)NULL, cx, NULL, t34, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t38);
        goto gm_fdone;
    }
    {
        void * t39 = gm_l0;
        void * t40 = (void *)((((uint8_t *)t39) + 1064) + 48);
        gm_tn68el8d14vku t41 = gm_l6;
        void * t42 = gm_l0;
        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)t42) + 1064));
        gm_trqfvmjtt4sij t44;
        gm_f_list_go2qg0b((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        gm_trqfvmjtt4sij t46;
        gm_f_list_go2qg0b((void *)&t46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t47 = gm_l3;
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 28));
        gm_to8g4620flbcm t49;
        GM_ST(uint32_t, (uint8_t *)&t49 + 20, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 68, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 124, 0);
        GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t49 + 0), t41);
        GM_ST(uint32_t, ((uint8_t *)&t49 + 16), t43);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t49 + 24), t44);
        GM_ST(uint32_t, ((uint8_t *)&t49 + 64), t45);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t49 + 72), t46);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t49 + 112), t48);
        gm_tftb1odc739t5 t50 = gm_f_push_t6u2ggv(t40, &t49);
        gm_l19 = t50;
    }
    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srckqas99ada0fa, 381, 5);
    {
        void * t52 = gm_l0;
        gm_tn68el8d14vku t53 = gm_l6;
        void * t54 = gm_l4;
        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        void * t56 = gm_l3;
        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 16));
        gm_tfim8ting9n8a t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t58 + 0), t54);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 8), t55);
        GM_ST(void *, ((uint8_t *)&t58 + 16), t57);
        gm_b16_8 t59 = gm_l5;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        gm_t8dl9beiqk70v t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tfim8ting9n8a, ((uint8_t *)&t61 + 8), t58);
        gm_tl2g3fsjpi9op t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t62 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t62 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
        uint32_t t63 = gm_f_new_fiber_39221jf((void *)NULL, cx, NULL, t52, t53, &t61, &t62, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t63;
    }
    {
        void * t64 = gm_l0;
        void * t65 = gm_l3;
        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 28));
        gm_tn68el8d14vku t67 = gm_l6;
        uint32_t t68 = gm_l7;
        gm_f_drive_i7nr5mu((void *)&gm_l8, (void *)NULL, cx, NULL, t64, t66, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t69 = gm_l0;
    void * t70 = gm_l3;
    gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 28));
    gm_tn68el8d14vku t72 = gm_l6;
    gm_b0_1 t73 = gm_f_abort_all_4him68f((void *)NULL, cx, NULL, t69, t71, t72);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = ((uint64_t)0ULL);
    void * t74 = gm_l0;
    uint64_t t75 = GM_LD(uint64_t, (((((uint8_t *)t74) + 1064) + 48) + 8));
    gm_l9 = t75;
    {
        for (;;) {
            uint64_t t76 = gm_l10;
            uint64_t t77 = gm_l9;
            uint8_t t78 = (t76 < t77);
            if (!t78) {
                goto gm_lb4;
            }
            {
                {
                    {
                        uint64_t t79 = gm_l10;
                        gm_l11 = t79;
                        void * t80 = gm_l0;
                        void * t81 = GM_LD(void *, ((((uint8_t *)t80) + 1064) + 48));
                        uint64_t t82 = GM_LD(uint64_t, (((((uint8_t *)t80) + 1064) + 48) + 8));
                        uint64_t t83 = gm_l11;
                        if ((uintptr_t)t83 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t83, t82, gm_srckqas99ada0fa, 387, 21);
                        gm_tn68el8d14vku t84 = GM_LD(gm_tn68el8d14vku, (((uint8_t *)t81) + (uintptr_t)t83 * 128));
                        gm_tn68el8d14vku t85 = gm_l6;
                        uint8_t t86 = gm_f_same_key_a5oh9pl(GM_CONV(gm_csff7attl2ehc5, t84), GM_CONV(gm_csff7attl2ehc5, t85));
                        if (t86) {
                            void * t87 = gm_l0;
                            void * t88 = (void *)((((uint8_t *)t87) + 1064) + 48);
                            uint64_t t89 = gm_l11;
                            gm_to8g4620flbcm t90;
                            gm_f_swap_remove_7083s0t((void *)&t90, t88, t89);
                            F.l12 = t90;
                            void * t91 = (void *)(((uint8_t *)&F.l12) + 24);
                            gm_f_deinit_padgogk(t91);
                            void * t92 = (void *)(((uint8_t *)&F.l12) + 72);
                            gm_f_deinit_padgogk(t92);
                            goto gm_lb4;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srckqas99ada0fa, 386, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t93 = gm_l10;
            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
            uint64_t t96 = r94;
            gm_l10 = t96;
        }
    }
gm_lb4:;
    uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t97) {
    } else {
        {
            memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l8) + 8), 24);
        }
        {
            {
                uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                uint8_t t99 = (uint8_t)(t98 == ((uint8_t)0ULL));
                if (!t99) {
                    goto gm_lb10;
                }
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                gm_l14 = t100;
                uint32_t t101 = GM_LD(uint32_t, ((((uint8_t *)&gm_l13) + 8) + 8));
                gm_l15 = t101;
                uint64_t t102 = gm_l14;
                void * t103 = gm_l4;
                uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)t103));
                uint8_t t105 = ((t102 == t104));
                if (!t105) {
                    goto gm_lb10;
                }
                uint32_t t106 = gm_l15;
                void * t107 = gm_l3;
                uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)t107) + 24));
                uint8_t t109 = ((t106 == t108));
                if (!t109) {
                    goto gm_lb10;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                goto gm_fdone;
            }
gm_lb10:;
        }
gm_lb9:;
        {
            gm_t76123gfet73g t110 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l13));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t110);
            goto gm_fdone;
        }
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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

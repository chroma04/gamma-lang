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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tcsu09l1pp5hf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; gm_t4c7ipjj9s3fp f3; } gm_td5sh6gra0q3b;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tg0vmr665gcag;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_t08olq97rscn3 f2; } gm_tqtt2php6e8lk;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tsu0mic7gtnv6;
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
typedef struct { void * f0; gm_tg0vmr665gcag f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t1og5k0mlcjk3;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
typedef struct { uint64_t f0; gm_tqtt2php6e8lk f1; } gm_t7qqq0mh1r8ke;
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tcsu09l1pp5hf) == 80 && _Alignof(gm_tcsu09l1pp5hf) == 8 && offsetof(gm_tcsu09l1pp5hf, f0) == 0 && offsetof(gm_tcsu09l1pp5hf, f1) == 16 && offsetof(gm_tcsu09l1pp5hf, f2) == 32 && offsetof(gm_tcsu09l1pp5hf, f3) == 72, "layout of `SourceFile`");
_Static_assert(sizeof(gm_td5sh6gra0q3b) == 96 && _Alignof(gm_td5sh6gra0q3b) == 8 && offsetof(gm_td5sh6gra0q3b, f0) == 0 && offsetof(gm_td5sh6gra0q3b, f1) == 16 && offsetof(gm_td5sh6gra0q3b, f2) == 32 && offsetof(gm_td5sh6gra0q3b, f3) == 40, "layout of `Scope`");
_Static_assert(sizeof(gm_tg0vmr665gcag) == 216 && _Alignof(gm_tg0vmr665gcag) == 8 && offsetof(gm_tg0vmr665gcag, f0) == 0 && offsetof(gm_tg0vmr665gcag, f1) == 40 && offsetof(gm_tg0vmr665gcag, f2) == 96 && offsetof(gm_tg0vmr665gcag, f3) == 136 && offsetof(gm_tg0vmr665gcag, f4) == 176, "layout of `FileFacts`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
typedef struct { gm_t4gut2c9fj1kt f0; uint8_t f1; uint8_t pe[7]; } gm_tkc9ngh8g1s0j;
typedef struct { uint64_t f0; gm_tcsu09l1pp5hf f1; } gm_tntfcv47jbeh7;
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
_Static_assert(sizeof(gm_tqtt2php6e8lk) == 56 && _Alignof(gm_tqtt2php6e8lk) == 8 && offsetof(gm_tqtt2php6e8lk, f0) == 0 && offsetof(gm_tqtt2php6e8lk, f1) == 8 && offsetof(gm_tqtt2php6e8lk, f2) == 24, "layout of `Parsed`");
_Static_assert(sizeof(gm_tsu0mic7gtnv6) == 56 && _Alignof(gm_tsu0mic7gtnv6) == 8 && offsetof(gm_tsu0mic7gtnv6, f0) == 0 && offsetof(gm_tsu0mic7gtnv6, f1) == 16, "layout of `Allow`");
_Static_assert(sizeof(gm_t1og5k0mlcjk3) == 280 && _Alignof(gm_t1og5k0mlcjk3) == 8 && offsetof(gm_t1og5k0mlcjk3, f0) == 0 && offsetof(gm_t1og5k0mlcjk3, f1) == 8 && offsetof(gm_t1og5k0mlcjk3, f2) == 224 && offsetof(gm_t1og5k0mlcjk3, f3) == 264, "layout of `Walk`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
_Static_assert(sizeof(gm_t7qqq0mh1r8ke) == 64 && _Alignof(gm_t7qqq0mh1r8ke) == 8 && offsetof(gm_t7qqq0mh1r8ke, f0) == 0 && offsetof(gm_t7qqq0mh1r8ke, f1) == 8, "layout of `(i: usize, x: Parsed)`");
_Static_assert(sizeof(gm_tkc9ngh8g1s0j) == 192 && _Alignof(gm_tkc9ngh8g1s0j) == 8 && offsetof(gm_tkc9ngh8g1s0j, f0) == 0 && offsetof(gm_tkc9ngh8g1s0j, f1) == 184, "layout of `?Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tntfcv47jbeh7) == 88 && _Alignof(gm_tntfcv47jbeh7) == 8 && offsetof(gm_tntfcv47jbeh7, f0) == 0 && offsetof(gm_tntfcv47jbeh7, f1) == 8, "layout of `(i: usize, x: SourceFile)`");
typedef struct { gm_tl5vm0tcqvdvb f0; uint8_t f1; uint8_t pe[7]; } gm_te3ir342llopq;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
_Static_assert(sizeof(gm_te3ir342llopq) == 56 && _Alignof(gm_te3ir342llopq) == 8 && offsetof(gm_te3ir342llopq, f0) == 0 && offsetof(gm_te3ir342llopq, f1) == 48, "layout of `?Site`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_canonical_2k2pqlg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_config_s3r9hnq {
    gm_frame h;
    gm_tin97tt4ud0s1 l0; /* c */
    gm_tk9n3vlila9qg l3; /* r */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_default_level_urberdg {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_deny_warnings_vk3pcl7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4u7p3q6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_s4sbegm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_root_vg2av48 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_finding_diag_2vp4thv {
    gm_frame h;
    gm_t4gut2c9fj1kt l8; /* d */
    gm_t4gut2c9fj1kt l10; /* d */
    gm_tl5vm0tcqvdvb l11; /* site */
    gm_tnvnsr1bfvtbd l17; /* fix */
    gm_tl5vm0tcqvdvb l22; /* site */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l36; /* d */
    gm_t4gut2c9fj1kt l38; /* d */
    gm_t4gut2c9fj1kt l45; /* d */
    gm_t4gut2c9fj1kt l50; /* d */
    gm_t4gut2c9fj1kt l57; /* d */
    gm_t4gut2c9fj1kt l63; /* d */
    gm_t4gut2c9fj1kt l69; /* d */
    gm_t4gut2c9fj1kt l76; /* d */
    gm_tkc9ngh8g1s0j ret;
};
struct gm_fr_format_331tgtc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40d716h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5mja5ge {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7e5ha9c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7h4sakv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a2pi3bt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b46cbct {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpa0si0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_duvvd80 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ec2c39v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_iqr642o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mqu5cbu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ojhiv11 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r4p6poi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rbr6trv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rrko99s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tovpseb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u8cjln1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udg5t2e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uv4nplt {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vft2h8j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_1ji4too {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s7cd6p5 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_in_scope_tm13267 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_level_m5neg79 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_level_name_qqj3mmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lint_diag_4vra4qu {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rbj3fr6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_level_4iat2k8 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l0rgbjf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_tf0cjh1 {
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
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hm4ber8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kfs9rll {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rule_scmud9h {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_run_n6la0ov {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* scoped */
    gm_tcsu09l1pp5hf l10; /* f */
    gm_trqfvmjtt4sij l11; /* findings */
    gm_trqfvmjtt4sij l12; /* found */
    gm_t1og5k0mlcjk3 l13; /* w */
    gm_t4c7ipjj9s3fp l14; /* when_names */
    gm_t4c7ipjj9s3fp l15; /* at */
    gm_tqtt2php6e8lk l19; /* p */
    gm_tcsu09l1pp5hf l23; /* f */
    gm_tj5f8vajf35o7 l29; /* f */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l34; /* d */
    gm_trqfvmjtt4sij l35; /* all */
    gm_t4gut2c9fj1kt l39; /* d */
    gm_tsu0mic7gtnv6 l44; /* a */
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_scope_al6a28r {
    gm_frame h;
    gm_td5sh6gra0q3b ret;
};
struct gm_fr_set_5cf9rlg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_text_of_lbn39it {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_walk_tech5q5 {
    gm_frame h;
    gm_t1og5k0mlcjk3 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_whole_lines_s23hlj6 {
    gm_frame h;
    gm_toceffb697p0u ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_dangling_notes_s4j764p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dangling_message_s0d8vok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
__attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_s4sbegm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_bhdmq1t(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ec2c39v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_iqr642o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mqu5cbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ojhiv11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_u8cjln1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uv4nplt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gk4t09c(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tf0cjh1(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_sokl06f(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm4ber8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4);
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_ciuj8hs0q3l[8] __attribute__((aligned(8)));
extern uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1fsfd0lcgf8e[17];
extern const uint8_t gm_s1gd0gkbe5tu9[55];
extern const uint8_t gm_s25qdppmkh7s6[7];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s4296alm81si3[14];
extern const uint8_t gm_s51jnn7biksj8[68];
extern const uint8_t gm_s57kk096d0ite[45];
extern const uint8_t gm_s5c6gmkd367jj[46];
extern const uint8_t gm_s5qukklsuqpi4[17];
extern const uint8_t gm_s6o3rebeo47ka[16];
extern const uint8_t gm_s6vfshjjhk2g7[19];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8uchl9nh4d11[33];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_sa0ep7oknkrm1[18];
extern const uint8_t gm_sa0f63dcuo6q9[57];
extern const uint8_t gm_sa7chm2nt7fsk[27];
extern const uint8_t gm_sbf1f7fu8bvpt[5];
extern const uint8_t gm_sbmlclkb2d9o9[74];
extern const uint8_t gm_sc46nvblu52f3[18];
extern const uint8_t gm_sc5ekkhtqttfb[4];
extern const uint8_t gm_sch3p0ffmutvn[16];
extern const uint8_t gm_scvmbht29gva9[7];
extern const uint8_t gm_se3smlbkt8d02[37];
extern const uint8_t gm_sed3pofb5bu9b[26];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_senuk3dngdds0[17];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sfj8otp2pq7qq[55];
extern const uint8_t gm_sg25rp8qmrbme[57];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgi6la00bpjt2[15];
extern const uint8_t gm_sgrd6s4kl4dhp[29];
extern const uint8_t gm_sgu334n5g1kog[16];
extern const uint8_t gm_shja5bmmh8ag2[30];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_si6c601mt9p4v[220];
extern const uint8_t gm_sidgpvfsv94q1[56];
extern const uint8_t gm_sijvlope91krv[31];
extern const uint8_t gm_siooblm685vmo[16];
extern const uint8_t gm_sirqj5pqb9rph[58];
extern const uint8_t gm_sisn94tbdoeof[65];
extern const uint8_t gm_siun1n8hqooca[9];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sl9dbf0dicmme[39];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_sldmbttac8k2j[59];
extern const uint8_t gm_slhuk1lrfcjqc[32];
extern const uint8_t gm_slnlsebkdpqrk[9];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_sp0qjak52qqef[158];
extern const uint8_t gm_spn8bpm132g2m[19];
extern const uint8_t gm_spp2t9pj35hcq[7];
extern const uint8_t gm_sq1d6esradpbv[21];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_srajaqomjf1d3[19];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuktvltncsesc[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srvn31r258t3t[16];
extern const uint8_t gm_ss49sa2bohvbp[28];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stooh1vej0ram[16];
extern const uint8_t gm_su3sef888krmb[22];
extern const uint8_t gm_sucfks6hb8bf7[44];
extern const uint8_t gm_sv242olhg8pke[13];
extern const uint8_t gm_sv44r8pinka4e[13];
extern const uint8_t gm_svo4dku3ftf1l[177];
extern const gm_tmpl gm_tpl00rv7vuig9ur;
extern const gm_tmpl gm_tpl1os40ii1iecn;
extern const gm_tmpl gm_tpl5hatm6q530pn;
extern const gm_tmpl gm_tpl7meurf74nbc1;
extern const gm_tmpl gm_tpl8un0qv11eeom;
extern const gm_tmpl gm_tplck4jpb6nd99p;
extern const gm_tmpl gm_tpldl4pn21ood35;
extern const gm_tmpl gm_tpldv5armigctqs;
extern const gm_tmpl gm_tplecdkophfbiam;
extern const gm_tmpl gm_tplekdseqnnupsj;
extern const gm_tmpl gm_tplengfdkso35ua;
extern const gm_tmpl gm_tplesi2enviniss;
extern const gm_tmpl gm_tplf8er2klne616;
extern const gm_tmpl gm_tplfsk62pc25cdb;
extern const gm_tmpl gm_tplfsstr25ojml5;
extern const gm_tmpl gm_tplibksf6f0e00q;
extern const gm_tmpl gm_tplisurri6t3aun;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tpln8gtl1e9t7j8;
extern const gm_tmpl gm_tplq12pt4e53uj3;
extern const gm_tmpl gm_tplrd5hip684e7e;
extern const gm_tmpl gm_tpludgprff7jihr;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_path_gj3a0ni F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_2k2pqlg F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_722uua4(t5, t6);
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_level_urberdg F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 32));
    if (t3) {
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        gm_l1 = t4;
    } else {
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
        gm_l1 = t5;
    }
    gm_t9qkh0rsooc8d t6 = gm_l1;
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4u7p3q6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_s4sbegm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_s4sbegm F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_kfs9rll(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 184);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_vg2av48 F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_331tgtc F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sldmbttac8k2j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5hatm6q530pn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_40d716h F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgi6la00bpjt2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplck4jpb6nd99p), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_5mja5ge F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sidgpvfsv94q1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpludgprff7jihr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7e5ha9c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sirqj5pqb9rph));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplisurri6t3aun), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7h4sakv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shja5bmmh8ag2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7meurf74nbc1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a2pi3bt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnlsebkdpqrk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl00rv7vuig9ur), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b46cbct F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sucfks6hb8bf7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8un0qv11eeom), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_duvvd80 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5c6gmkd367jj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplecdkophfbiam), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ec2c39v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ec2c39v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slhuk1lrfcjqc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1os40ii1iecn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_iqr642o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_iqr642o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa0f63dcuo6q9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldv5armigctqs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mqu5cbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mqu5cbu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp0qjak52qqef));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(157));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplekdseqnnupsj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ojhiv11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ojhiv11 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8uchl9nh4d11));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfsk62pc25cdb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r4p6poi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sed3pofb5bu9b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplq12pt4e53uj3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rbr6trv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se3smlbkt8d02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln8gtl1e9t7j8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rrko99s F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si6c601mt9p4v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(219));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplibksf6f0e00q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tovpseb F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg25rp8qmrbme));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfsstr25ojml5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_u8cjln1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u8cjln1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s57kk096d0ite));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf8er2klne616), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_udg5t2e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scvmbht29gva9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrd5hip684e7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uv4nplt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uv4nplt F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss49sa2bohvbp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplesi2enviniss), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vft2h8j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sisn94tbdoeof));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplengfdkso35ua), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_m5neg79 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_rule_scmud9h(t2);
    gm_l3 = t3;
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t5;
    } else {
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l2;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_srcuktvltncsesc, 80, 12);
    gm_t9qkh0rsooc8d t11 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + (uintptr_t)t10 * 1));
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_diag_4vra4qu F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_f_lint_eo1tub5((void *)&(*(gm_t4gut2c9fj1kt *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_eo1tub5 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_f_warning_q0m5jjs((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 160), t5);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 184);
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rbj3fr6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4u7p3q6();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n5n7pq1();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l0rgbjf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1th396a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kfs9rll(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)&F.l1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tf0cjh1(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_tf0cjh1 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_4u7p3q6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hm4ber8(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm4ber8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hm4ber8 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kfs9rll F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rule_scmud9h F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_tf5srh4mp4b3i t3 = gm_f_lint_rule_o3ccae1(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_scope_al6a28r F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* std_root */
    uint8_t gm_l2; /* linting_std */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_p3t5tj8((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 16), t3);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 32), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 40), t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_f_strip_prefix_vl3t0h4((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_of_lbn39it F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 16) + 8));
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t8 = (uint64_t)t7;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t10 = (uint64_t)t9;
    uint64_t t11 = (uintptr_t)t8;
    uint64_t t12 = (uintptr_t)t10;
    if (t11 > t12 || t12 > (uintptr_t)t6) gm_trap_slice(t11, t12, t6, gm_srcuktvltncsesc, 145, 76);
    uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)t8;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t5 + (uintptr_t)t8 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    F.ret = t14;
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
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_walk_tech5q5 F;
    void * gm_l0; /* sm */
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_tg0vmr665gcag t3;
    gm_f_file_facts_tdo30n8((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_uaa66su((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_qbupbr6();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(void *, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 0), t2);
    GM_ST(gm_tg0vmr665gcag, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 8), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 224), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 264), t6);
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
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_warning_q0m5jjs F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l2), t4);
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 184);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_default_config_s3r9hnq F;
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 215u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_rbj3fr6((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t8;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 0), t7);
        F.l0 = t8;
    }
    void * t9 = (void *)((uint8_t *)gm_cqss81ml8qn);
    uint64_t t10 = ((uintptr_t)((uint64_t)23ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t9 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
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
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t15) + (uintptr_t)t17 * 40), 40);
                        void * t18 = (void *)((uint8_t *)&F.l0);
                        void * t19 = (void *)((uint8_t *)&F.l3);
                        gm_t9qkh0rsooc8d t20 = gm_f_default_level_urberdg(t19);
                        gm_tftb1odc739t5 t21 = gm_f_push_tf0cjh1(t18, t20);
                        gm_l4 = t21;
                        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcuktvltncsesc, 55, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 55, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l2;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l2 = t26;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&F.l0), 40);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
inline __attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deny_warnings_vk3pcl7 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)0ULL);
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)gm_ciuj8hs0q3l));
    gm_l1 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = gm_l1;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t11 = gm_l2;
                        gm_l3 = t11;
                        void * t12 = gm_l0;
                        void * t13 = GM_LD(void *, ((uint8_t *)t12));
                        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
                        uint64_t t15 = gm_l3;
                        if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcuktvltncsesc, 74, 12);
                        gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
                        uint8_t t18 = (uint8_t)((t17 == ((uint8_t)1ULL)));
                        gm_t9qkh0rsooc8d t19;
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
                        if (t18) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l3;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcuktvltncsesc, 74, 41);
                            gm_t9qkh0rsooc8d t24;
                            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t21) + (uintptr_t)t23 * 1), t24);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 73, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l2;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l2 = t28;
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
extern __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_finding_diag_2vp4thv F;
    void * gm_l0; /* sm */
    void * gm_l1; /* f */
    void * gm_l2; /* sites */
    void * gm_l3; /* when_names */
    gm_b16_8 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint8_t gm_l6; /* param */
    uint8_t gm_l7; /* assigned */
    gm_b16_8 gm_l9; /* msg */
    uint8_t gm_l12; /* typed */
    gm_toceffb697p0u gm_l13; /* op */
    gm_tl874t2atnlh3 gm_l14; /* o */
    gm_tl874t2atnlh3 gm_l15; /* at */
    gm_b16_8 gm_l16; /* to */
    gm_tbgd14esf0jvi gm_l18; /* p */
    gm_b16_8 gm_l19; /* what */
    gm_b16_8 gm_l20; /* name */
    gm_tl874t2atnlh3 gm_l21; /* span */
    uint8_t gm_l23; /* mutable */
    uint8_t gm_l24; /* typed */
    gm_toceffb697p0u gm_l25; /* op */
    uint8_t gm_l26; /* keep_mutable */
    uint8_t gm_l27; /* fn_lit */
    uint8_t gm_l28; /* uninit */
    gm_b16_8 gm_l29; /* marker */
    gm_tl874t2atnlh3 gm_l31; /* o */
    gm_b16_8 gm_l32; /* name */
    gm_tl874t2atnlh3 gm_l33; /* span */
    gm_tl874t2atnlh3 gm_l34; /* item */
    gm_b16_8 gm_l35; /* what */
    gm_tl874t2atnlh3 gm_l37; /* lines */
    gm_b16_8 gm_l39; /* name */
    gm_tl874t2atnlh3 gm_l40; /* span */
    gm_t9qkh0rsooc8d gm_l41; /* vis */
    gm_b16_8 gm_l42; /* what */
    gm_b16_8 gm_l43; /* marker */
    gm_b16_8 gm_l44; /* why */
    gm_tl874t2atnlh3 gm_l46; /* span */
    gm_b16_8 gm_l47; /* name */
    gm_tl874t2atnlh3 gm_l48; /* local */
    gm_b16_8 gm_l49; /* why */
    gm_tl874t2atnlh3 gm_l51; /* span */
    gm_b16_8 gm_l52; /* name */
    gm_tl874t2atnlh3 gm_l53; /* catch_all */
    uint8_t gm_l54; /* is_result */
    gm_b16_8 gm_l55; /* value */
    gm_b16_8 gm_l56; /* absent */
    gm_tl874t2atnlh3 gm_l58; /* span */
    gm_b16_8 gm_l59; /* name */
    gm_tl874t2atnlh3 gm_l60; /* local */
    gm_tl874t2atnlh3 gm_l61; /* origin */
    gm_b16_8 gm_l62; /* src */
    gm_tl874t2atnlh3 gm_l64; /* span */
    gm_b16_8 gm_l65; /* name */
    gm_tl874t2atnlh3 gm_l66; /* list */
    gm_tl874t2atnlh3 gm_l67; /* growth */
    gm_b16_8 gm_l68; /* src */
    gm_tl874t2atnlh3 gm_l70; /* span */
    gm_tl874t2atnlh3 gm_l71; /* operand */
    uint8_t gm_l72; /* same */
    gm_b16_8 gm_l73; /* whole */
    gm_b16_8 gm_l74; /* o */
    gm_b16_8 gm_l75; /* replacement */
    gm_tl874t2atnlh3 gm_l77; /* span */
    uint8_t gm_l78; /* always */
    gm_tl874t2atnlh3 gm_l79; /* span */
    GM_STACK_CHECK(&F, 10373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_tabb710d9mq2e gm_l82;
    gm_tabb710d9mq2e gm_l83;
    gm_te3ir342llopq gm_l84;
    gm_tabb710d9mq2e gm_l85;
    gm_tabb710d9mq2e gm_l86;
    gm_te3ir342llopq gm_l87;
    uint8_t gm_l88;
    uint8_t gm_l89;
    gm_tabb710d9mq2e gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_toceffb697p0u gm_l92;
    gm_tabb710d9mq2e gm_l93;
    gm_tabb710d9mq2e gm_l94;
    gm_tabb710d9mq2e gm_l95;
    gm_tabb710d9mq2e gm_l96;
    gm_tabb710d9mq2e gm_l97;
    gm_tabb710d9mq2e gm_l98;
    gm_tabb710d9mq2e gm_l99;
    gm_tabb710d9mq2e gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_tabb710d9mq2e gm_l102;
    uint8_t gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    gm_tabb710d9mq2e gm_l106;
    gm_tabb710d9mq2e gm_l107;
    gm_tabb710d9mq2e gm_l108;
    gm_tabb710d9mq2e gm_l109;
    {
        void * t32 = gm_l1;
        {
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                    if (!t34) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l4 = t35;
                    gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l5 = t36;
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 28));
                    gm_l6 = t37;
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 29));
                    gm_l7 = t38;
                    void * t39 = gm_l3;
                    gm_b16_8 t40 = gm_l4;
                    uint8_t t41 = gm_f_contains_57n7jqd(t39, t40);
                    if (t41) {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    uint8_t t42 = gm_l6;
                    if (t42) {
                        {
                            gm_tl874t2atnlh3 t43 = gm_l5;
                            gm_b16_8 t44;
                            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s5qukklsuqpi4));
                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(16));
                            gm_b16_8 t45 = gm_l4;
                            gm_tnpifv20irnph t46;
                            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                            gm_f_format_ojhiv11((void *)&gm_l80, (void *)NULL, cx, NULL, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 8, gm_srcuktvltncsesc, 215, 58);
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            gm_t4gut2c9fj1kt t50;
                            gm_f_lint_diag_4vra4qu((void *)&t50, (void *)NULL, cx, NULL, t43, t44, t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l8 = t50;
                        }
                        {
                            void * t51 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t52 = gm_l4;
                            gm_tnpifv20irnph t53;
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                            gm_f_format_b46cbct((void *)&gm_l81, (void *)NULL, cx, NULL, t53);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srcuktvltncsesc, 216, 28);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                            gm_f_add_note_46v502s(t51, t56);
                        }
                        {
                            gm_t4gut2c9fj1kt t57 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                            GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t57);
                            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                            goto gm_fdone;
                        }
                    }
                    uint8_t t58 = gm_l7;
                    if (t58) {
                        gm_b16_8 t59 = gm_l4;
                        gm_tnpifv20irnph t60;
                        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
                        gm_f_format_u8cjln1((void *)&gm_l82, (void *)NULL, cx, NULL, t60);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                        if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 8, gm_srcuktvltncsesc, 220, 17);
                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                        gm_l9 = t62;
                    } else {
                        gm_b16_8 t63 = gm_l4;
                        gm_tnpifv20irnph t64;
                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
                        gm_f_format_ec2c39v((void *)&gm_l83, (void *)NULL, cx, NULL, t64);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 8, gm_srcuktvltncsesc, 222, 17);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                        gm_l9 = t66;
                    }
                    {
                        gm_tl874t2atnlh3 t67 = gm_l5;
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_stooh1vej0ram));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(15));
                        gm_b16_8 t69 = gm_l9;
                        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                        gm_t4gut2c9fj1kt t71;
                        gm_f_lint_diag_4vra4qu((void *)&t71, (void *)NULL, cx, NULL, t67, t68, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l10 = t71;
                    }
                    {
                        {
                            {
                                void * t72 = gm_l2;
                                gm_tl874t2atnlh3 t73 = gm_l5;
                                gm_f_get_gk4t09c((void *)&gm_l84, t72, GM_CONV(gm_cseahvg8v7p0vs, t73));
                            }
                            uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l84))[48] != 0);
                            if (!t74) {
                                goto gm_lb4;
                            }
                            {
                                memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l84), 48);
                            }
                            {
                                {
                                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)0ULL));
                                    if (!t76) {
                                        goto gm_lb6;
                                    }
                                    uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 8) + 1));
                                    gm_l12 = t77;
                                    gm_toceffb697p0u t78 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l11) + 8) + 4));
                                    gm_l13 = t78;
                                    {
                                        {
                                            uint8_t t79 = gm_l12;
                                            uint8_t t80 = (uint8_t)!t79;
                                            if (!t80) {
                                                goto gm_lb8;
                                            }
                                            uint8_t t81 = gm_l7;
                                            uint8_t t82 = (uint8_t)!t81;
                                            if (!t82) {
                                                goto gm_lb8;
                                            }
                                            {
                                                {
                                                    uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l13))[12] != 0);
                                                    if (!t83) {
                                                        goto gm_lb10;
                                                    }
                                                    gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l13));
                                                    gm_l14 = t84;
                                                    void * t85 = (void *)((uint8_t *)&F.l10);
                                                    gm_b16_8 t86;
                                                    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sgrd6s4kl4dhp));
                                                    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(28));
                                                    uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                                                    uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                                    uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 8));
                                                    gm_tl874t2atnlh3 t90;
                                                    GM_ST(uint32_t, ((uint8_t *)&t90 + 0), t87);
                                                    GM_ST(uint32_t, ((uint8_t *)&t90 + 4), t88);
                                                    GM_ST(uint32_t, ((uint8_t *)&t90 + 8), t89);
                                                    gm_b16_8 t91;
                                                    GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sc5ekkhtqttfb));
                                                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(3));
                                                    gm_b0_1 t92 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t85, t86, t90, t91);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    {
                                                        gm_t4gut2c9fj1kt t93 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                                                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t93);
                                                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                                                        goto gm_fdone;
                                                    }
                                                }
gm_lb10:;
                                            }
gm_lb9:;
                                            goto gm_lb7;
                                        }
gm_lb8:;
                                    }
gm_lb7:;
                                    goto gm_lb5;
                                }
gm_lb6:;
                                {
                                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)1ULL));
                                    if (!t95) {
                                        goto gm_lb11;
                                    }
                                    gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 8));
                                    gm_l15 = t96;
                                    gm_b16_8 t97 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 8) + 16));
                                    gm_l16 = t97;
                                    uint8_t t98 = gm_l7;
                                    uint8_t t99 = (uint8_t)!t98;
                                    if (t99) {
                                        {
                                            void * t100 = (void *)((uint8_t *)&F.l10);
                                            gm_b16_8 t101 = gm_l16;
                                            gm_tnpifv20irnph t102;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t101);
                                            gm_f_format_ooibcq2((void *)&gm_l85, (void *)NULL, cx, NULL, t102);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                                            if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 8, gm_srcuktvltncsesc, 234, 35);
                                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                                            gm_tl874t2atnlh3 t106 = gm_l15;
                                            gm_b16_8 t107 = gm_l16;
                                            gm_b0_1 t108 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t100, t105, t106, t107);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        {
                                            gm_t4gut2c9fj1kt t109 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                                            GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t109);
                                            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                                            goto gm_fdone;
                                        }
                                    }
                                    goto gm_lb5;
                                }
gm_lb11:;
                                {
                                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)2ULL));
                                    if (!t111) {
                                        goto gm_lb12;
                                    }
                                    memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l11) + 8), 40);
                                    {
                                        {
                                            uint8_t t112 = (uint8_t)((((uint8_t *)&F.l17))[32] != 0);
                                            if (!t112) {
                                                goto gm_lb14;
                                            }
                                            {
                                                memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l17), 32);
                                            }
                                            uint64_t t113 = GM_LD(uint64_t, ((((uint8_t *)&gm_l18) + 16) + 8));
                                            uint8_t t114 = ((t113 == ((uint64_t)0ULL)));
                                            if (t114) {
                                                gm_b16_8 t115;
                                                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s6vfshjjhk2g7));
                                                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(18));
                                                gm_l19 = t115;
                                            } else {
                                                gm_b16_8 t116;
                                                GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sa0ep7oknkrm1));
                                                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(17));
                                                gm_l19 = t116;
                                            }
                                            void * t117 = (void *)((uint8_t *)&F.l10);
                                            gm_b16_8 t118 = gm_l19;
                                            gm_tl874t2atnlh3 t119 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l18));
                                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                                            gm_b0_1 t121 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t117, t118, t119, t120);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            {
                                                gm_t4gut2c9fj1kt t122 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                                                GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t122);
                                                ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                                                goto gm_fdone;
                                            }
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    goto gm_lb5;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 226, 17);
                            }
gm_lb5:;
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    {
                        void * t123 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t124 = gm_l4;
                        gm_tnpifv20irnph t125;
                        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
                        gm_f_format_rbr6trv((void *)&gm_l86, (void *)NULL, cx, NULL, t125);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                        if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 8, gm_srcuktvltncsesc, 244, 24);
                        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                        gm_f_add_note_46v502s(t123, t128);
                    }
                    {
                        gm_t4gut2c9fj1kt t129 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t129);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)1ULL));
                    if (!t131) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l20 = t132;
                    gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l21 = t133;
                    void * t134 = gm_l3;
                    gm_b16_8 t135 = gm_l20;
                    uint8_t t136 = gm_f_contains_57n7jqd(t134, t135);
                    if (t136) {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    void * t137 = gm_l2;
                    gm_tl874t2atnlh3 t138 = gm_l21;
                    gm_f_get_gk4t09c((void *)&gm_l87, t137, GM_CONV(gm_cseahvg8v7p0vs, t138));
                    uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l87))[48] != 0);
                    if (t139) {
                        memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l87), 48);
                    } else {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    {
                        {
                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                            uint8_t t141 = (uint8_t)(t140 == ((uint8_t)0ULL));
                            if (!t141) {
                                goto gm_lb17;
                            }
                            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 8));
                            gm_l23 = t142;
                            uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 1));
                            gm_l24 = t143;
                            gm_toceffb697p0u t144 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l22) + 8) + 4));
                            gm_l25 = t144;
                            uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 20));
                            gm_l26 = t145;
                            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 21));
                            gm_l27 = t146;
                            uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 22));
                            gm_l28 = t147;
                            uint8_t t148 = gm_l23;
                            uint8_t t149 = (uint8_t)!t148;
                            if (t149) {
                                gm_l89 = ((uint8_t)1);
                            } else {
                                uint8_t t150 = gm_l27;
                                gm_l89 = t150;
                            }
                            uint8_t t151 = gm_l89;
                            if (t151) {
                                gm_l88 = ((uint8_t)1);
                            } else {
                                uint8_t t152 = gm_l28;
                                gm_l88 = t152;
                            }
                            uint8_t t153 = gm_l88;
                            if (t153) {
                                memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                                goto gm_fdone;
                            }
                            uint8_t t154 = gm_l24;
                            if (t154) {
                                gm_b16_8 t155;
                                GM_ST(void *, (uint8_t *)&t155, (void *)(gm_s39mljh669qgl));
                                GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(1));
                                gm_l29 = t155;
                            } else {
                                gm_b16_8 t156;
                                GM_ST(void *, (uint8_t *)&t156, (void *)(gm_ss67f0gmfen1m));
                                GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(2));
                                gm_l29 = t156;
                            }
                            {
                                gm_tl874t2atnlh3 t157 = gm_l21;
                                gm_b16_8 t158;
                                GM_ST(void *, (uint8_t *)&t158, (void *)(gm_senuk3dngdds0));
                                GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(16));
                                gm_b16_8 t159 = gm_l20;
                                gm_b16_8 t160 = gm_l29;
                                gm_t6j6smj7qoctu t161;
                                GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t159);
                                GM_ST(gm_b16_8, ((uint8_t *)&t161 + 16), t160);
                                gm_f_format_7e5ha9c((void *)&gm_l90, (void *)NULL, cx, NULL, &t161);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t162 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                                if (!t162) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 8, gm_srcuktvltncsesc, 253, 54);
                                gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l90) + 8));
                                gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
                                gm_t4gut2c9fj1kt t165;
                                gm_f_lint_diag_4vra4qu((void *)&t165, (void *)NULL, cx, NULL, t157, t158, t164);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l30 = t165;
                            }
                            {
                                {
                                    uint8_t t166 = (uint8_t)((((uint8_t *)&gm_l25))[12] != 0);
                                    if (!t166) {
                                        goto gm_lb19;
                                    }
                                    gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l25));
                                    gm_l31 = t167;
                                    uint8_t t168 = gm_l26;
                                    uint8_t t169 = (uint8_t)!t168;
                                    if (t169) {
                                        void * t170 = (void *)((uint8_t *)&F.l30);
                                        gm_b16_8 t171;
                                        GM_ST(void *, (uint8_t *)&t171, (void *)(gm_sa7chm2nt7fsk));
                                        GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(26));
                                        gm_tl874t2atnlh3 t172 = gm_l31;
                                        gm_b16_8 t173 = gm_l29;
                                        gm_b0_1 t174 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t170, t171, t172, t173);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb18;
                                }
gm_lb19:;
                            }
gm_lb18:;
                            {
                                gm_t4gut2c9fj1kt t175 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l30));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                                GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t175);
                                ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                                goto gm_fdone;
                            }
                        }
gm_lb17:;
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
gm_lb16:;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t177 = (uint8_t)(t176 == ((uint8_t)2ULL));
                    if (!t177) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l32 = t178;
                    gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l33 = t179;
                    gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 28));
                    gm_l34 = t180;
                    gm_b16_8 t181 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 40));
                    gm_l35 = t181;
                    void * t182 = gm_l3;
                    gm_b16_8 t183 = gm_l32;
                    uint8_t t184 = gm_f_contains_57n7jqd(t182, t183);
                    if (t184) {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    gm_b16_8 t185 = gm_l35;
                    gm_b16_8 t186;
                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sg3kpa3h16no3));
                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(6));
                    uint8_t t187 = (gm_eq0iqiubfm7e(((uint8_t *)&t185), ((uint8_t *)&t186)));
                    if (t187) {
                        {
                            gm_tl874t2atnlh3 t188 = gm_l33;
                            gm_b16_8 t189;
                            GM_ST(void *, (uint8_t *)&t189, (void *)(gm_s4296alm81si3));
                            GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(13));
                            gm_b16_8 t190 = gm_l32;
                            gm_tnpifv20irnph t191;
                            GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t190);
                            gm_f_format_7h4sakv((void *)&gm_l91, (void *)NULL, cx, NULL, t191);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                            if (!t192) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcuktvltncsesc, 262, 55);
                            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                            gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
                            gm_t4gut2c9fj1kt t195;
                            gm_f_lint_diag_4vra4qu((void *)&t195, (void *)NULL, cx, NULL, t188, t189, t194);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l36 = t195;
                        }
                        {
                            {
                                void * t196 = gm_l0;
                                gm_tl874t2atnlh3 t197 = gm_l34;
                                gm_toceffb697p0u t198 = gm_f_whole_lines_s23hlj6(t196, GM_CONV(gm_cseahvg8v7p0vs, t197));
                                gm_l92 = t198;
                                uint8_t t199 = (uint8_t)((((uint8_t *)&gm_l92))[12] != 0);
                                if (!t199) {
                                    goto gm_lb22;
                                }
                                gm_tl874t2atnlh3 t200 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l92));
                                gm_l37 = t200;
                                void * t201 = (void *)((uint8_t *)&F.l36);
                                gm_b16_8 t202;
                                GM_ST(void *, (uint8_t *)&t202, (void *)(gm_sc46nvblu52f3));
                                GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(17));
                                gm_tl874t2atnlh3 t203 = gm_l37;
                                gm_b16_8 t204;
                                GM_ST(void *, (uint8_t *)&t204, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(0));
                                gm_b0_1 t205 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t201, t202, t203, t204);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                        }
gm_lb21:;
                        {
                            gm_t4gut2c9fj1kt t206 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l36));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                            GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t206);
                            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_tl874t2atnlh3 t207 = gm_l33;
                        gm_b16_8 t208;
                        GM_ST(void *, (uint8_t *)&t208, (void *)(gm_spn8bpm132g2m));
                        GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(18));
                        gm_b16_8 t209 = gm_l35;
                        gm_b16_8 t210 = gm_l32;
                        gm_t6j6smj7qoctu t211;
                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t209);
                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 16), t210);
                        gm_f_format_r4p6poi((void *)&gm_l93, (void *)NULL, cx, NULL, &t211);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                        if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 8, gm_srcuktvltncsesc, 266, 56);
                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l93) + 8));
                        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                        gm_t4gut2c9fj1kt t215;
                        gm_f_lint_diag_4vra4qu((void *)&t215, (void *)NULL, cx, NULL, t207, t208, t214);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l38 = t215;
                    }
                    void * t216 = (void *)((uint8_t *)&F.l38);
                    gm_b16_8 t217;
                    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sbmlclkb2d9o9));
                    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(73));
                    gm_f_add_note_46v502s(t216, t217);
                    {
                        gm_t4gut2c9fj1kt t218 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l38));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t218);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)6ULL));
                    if (!t220) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l39 = t221;
                    gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l40 = t222;
                    gm_t9qkh0rsooc8d t223 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t32) + 8) + 28));
                    gm_l41 = t223;
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 32));
                    gm_l42 = t224;
                    void * t225 = gm_l3;
                    gm_b16_8 t226 = gm_l39;
                    uint8_t t227 = gm_f_contains_57n7jqd(t225, t226);
                    if (t227) {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    gm_b16_8 t228;
                    GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sjqs7q3ostqbm));
                    GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(3));
                    gm_l43 = t228;
                    gm_b16_8 t229;
                    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s51jnn7biksj8));
                    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(67));
                    gm_l44 = t229;
                    gm_t9qkh0rsooc8d t230 = gm_l41;
                    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&t230) + 0));
                    uint8_t t232 = (uint8_t)((t231 == ((uint8_t)1ULL)));
                    gm_t9qkh0rsooc8d t233;
                    GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)1ULL));
                    if (t232) {
                        gm_b16_8 t234;
                        GM_ST(void *, (uint8_t *)&t234, (void *)(gm_shofa8pdml9fb));
                        GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(12));
                        gm_l43 = t234;
                        gm_b16_8 t235;
                        GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sl9dbf0dicmme));
                        GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(38));
                        gm_l44 = t235;
                    }
                    {
                        gm_tl874t2atnlh3 t236 = gm_l40;
                        gm_b16_8 t237;
                        GM_ST(void *, (uint8_t *)&t237, (void *)(gm_sv44r8pinka4e));
                        GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(12));
                        gm_b16_8 t238 = gm_l42;
                        gm_b16_8 t239 = gm_l39;
                        gm_b16_8 t240 = gm_l43;
                        gm_b16_8 t241 = gm_l44;
                        gm_t0uuhaljsf410 t242;
                        GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t238);
                        GM_ST(gm_b16_8, ((uint8_t *)&t242 + 16), t239);
                        GM_ST(gm_b16_8, ((uint8_t *)&t242 + 32), t240);
                        GM_ST(gm_b16_8, ((uint8_t *)&t242 + 48), t241);
                        gm_f_format_uv4nplt((void *)&gm_l94, (void *)NULL, cx, NULL, &t242);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                        if (!t243) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcuktvltncsesc, 278, 50);
                        gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                        gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                        gm_t4gut2c9fj1kt t246;
                        gm_f_lint_diag_4vra4qu((void *)&t246, (void *)NULL, cx, NULL, t236, t237, t245);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l45 = t246;
                    }
                    void * t247 = (void *)((uint8_t *)&F.l45);
                    gm_b16_8 t248;
                    GM_ST(void *, (uint8_t *)&t248, (void *)(gm_sfj8otp2pq7qq));
                    GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(54));
                    gm_f_add_note_46v502s(t247, t248);
                    {
                        gm_t4gut2c9fj1kt t249 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l45));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t249);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb23:;
                {
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t251 = (uint8_t)(t250 == ((uint8_t)7ULL));
                    if (!t251) {
                        goto gm_lb24;
                    }
                    gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l46 = t252;
                    gm_b16_8 t253 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l47 = t253;
                    gm_tl874t2atnlh3 t254 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l48 = t254;
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 48));
                    gm_l49 = t255;
                    {
                        gm_tl874t2atnlh3 t256 = gm_l46;
                        gm_b16_8 t257;
                        GM_ST(void *, (uint8_t *)&t257, (void *)(gm_s1fsfd0lcgf8e));
                        GM_ST(uintptr_t, (uint8_t *)&t257 + 8, (uintptr_t)(16));
                        gm_b16_8 t258 = gm_l47;
                        gm_b16_8 t259 = gm_l49;
                        gm_b16_8 t260 = gm_f_dangling_message_s0d8vok((void *)NULL, cx, NULL, t258, t259);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t4gut2c9fj1kt t261;
                        gm_f_lint_diag_4vra4qu((void *)&t261, (void *)NULL, cx, NULL, t256, t257, t260);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l50 = t261;
                    }
                    void * t262 = (void *)((uint8_t *)&F.l50);
                    gm_b16_8 t263 = gm_l47;
                    gm_tl874t2atnlh3 t264 = gm_l48;
                    gm_b0_1 t265 = gm_f_add_dangling_notes_s4j764p((void *)NULL, cx, NULL, t262, t263, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        gm_t4gut2c9fj1kt t266 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l50));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t266);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t268 = (uint8_t)(t267 == ((uint8_t)8ULL));
                    if (!t268) {
                        goto gm_lb25;
                    }
                    gm_tl874t2atnlh3 t269 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l51 = t269;
                    gm_b16_8 t270 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l52 = t270;
                    gm_tl874t2atnlh3 t271 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l53 = t271;
                    uint8_t t272 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 44));
                    gm_l54 = t272;
                    uint8_t t273 = gm_l54;
                    if (t273) {
                        gm_b16_8 t274;
                        GM_ST(void *, (uint8_t *)&t274, (void *)(gm_sch3p0ffmutvn));
                        GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(15));
                        gm_l55 = t274;
                    } else {
                        gm_b16_8 t275;
                        GM_ST(void *, (uint8_t *)&t275, (void *)(gm_s6o3rebeo47ka));
                        GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(15));
                        gm_l55 = t275;
                    }
                    uint8_t t276 = gm_l54;
                    if (t276) {
                        gm_b16_8 t277;
                        GM_ST(void *, (uint8_t *)&t277, (void *)(gm_siun1n8hqooca));
                        GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(8));
                        gm_l56 = t277;
                    } else {
                        gm_b16_8 t278;
                        GM_ST(void *, (uint8_t *)&t278, (void *)(gm_s25qdppmkh7s6));
                        GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(6));
                        gm_l56 = t278;
                    }
                    {
                        gm_tl874t2atnlh3 t279 = gm_l51;
                        gm_b16_8 t280;
                        GM_ST(void *, (uint8_t *)&t280, (void *)(gm_sq1d6esradpbv));
                        GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(20));
                        gm_b16_8 t281 = gm_l52;
                        gm_b16_8 t282 = gm_l55;
                        gm_b16_8 t283 = gm_l56;
                        gm_tfeab279c16rr t284;
                        GM_ST(gm_b16_8, ((uint8_t *)&t284 + 0), t281);
                        GM_ST(gm_b16_8, ((uint8_t *)&t284 + 16), t282);
                        GM_ST(gm_b16_8, ((uint8_t *)&t284 + 32), t283);
                        gm_f_format_331tgtc((void *)&gm_l95, (void *)NULL, cx, NULL, &t284);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                        if (!t285) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 8, gm_srcuktvltncsesc, 290, 58);
                        gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l95) + 8));
                        gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                        gm_t4gut2c9fj1kt t288;
                        gm_f_lint_diag_4vra4qu((void *)&t288, (void *)NULL, cx, NULL, t279, t280, t287);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l57 = t288;
                    }
                    {
                        void * t289 = (void *)((uint8_t *)&F.l57);
                        gm_tl874t2atnlh3 t290 = gm_l53;
                        gm_b16_8 t291 = gm_l56;
                        gm_tnpifv20irnph t292;
                        GM_ST(gm_b16_8, ((uint8_t *)&t292 + 0), t291);
                        gm_f_format_cpa0si0((void *)&gm_l96, (void *)NULL, cx, NULL, t292);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t293 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                        if (!t293) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 8, gm_srcuktvltncsesc, 291, 36);
                        gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l96) + 8));
                        gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                        gm_f_add_label_50fj7vv(t289, GM_CONV(gm_cseahvg8v7p0vs, t290), t295);
                    }
                    {
                        void * t296 = (void *)((uint8_t *)&F.l57);
                        gm_b16_8 t297 = gm_l52;
                        gm_tnpifv20irnph t298;
                        GM_ST(gm_b16_8, ((uint8_t *)&t298 + 0), t297);
                        gm_f_format_rrko99s((void *)&gm_l97, (void *)NULL, cx, NULL, t298);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t299 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                        if (!t299) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 8, gm_srcuktvltncsesc, 292, 24);
                        gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l97) + 8));
                        gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                        gm_f_add_note_46v502s(t296, t301);
                    }
                    {
                        gm_t4gut2c9fj1kt t302 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l57));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t302);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb25:;
                {
                    uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t304 = (uint8_t)(t303 == ((uint8_t)9ULL));
                    if (!t304) {
                        goto gm_lb26;
                    }
                    gm_tl874t2atnlh3 t305 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l58 = t305;
                    gm_b16_8 t306 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l59 = t306;
                    gm_tl874t2atnlh3 t307 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l60 = t307;
                    gm_tl874t2atnlh3 t308 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 44));
                    gm_l61 = t308;
                    void * t309 = gm_l0;
                    gm_tl874t2atnlh3 t310 = gm_l61;
                    gm_b16_8 t311 = gm_f_text_of_lbn39it(t309, GM_CONV(gm_cseahvg8v7p0vs, t310));
                    gm_l62 = t311;
                    {
                        gm_tl874t2atnlh3 t312 = gm_l58;
                        gm_b16_8 t313;
                        GM_ST(void *, (uint8_t *)&t313, (void *)(gm_sv242olhg8pke));
                        GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(12));
                        gm_b16_8 t314 = gm_l59;
                        gm_b16_8 t315 = gm_l62;
                        gm_t6j6smj7qoctu t316;
                        GM_ST(gm_b16_8, ((uint8_t *)&t316 + 0), t314);
                        GM_ST(gm_b16_8, ((uint8_t *)&t316 + 16), t315);
                        gm_f_format_vft2h8j((void *)&gm_l98, (void *)NULL, cx, NULL, &t316);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                        if (!t317) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 8, gm_srcuktvltncsesc, 297, 50);
                        gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l98) + 8));
                        gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                        gm_t4gut2c9fj1kt t320;
                        gm_f_lint_diag_4vra4qu((void *)&t320, (void *)NULL, cx, NULL, t312, t313, t319);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l63 = t320;
                    }
                    {
                        void * t321 = (void *)((uint8_t *)&F.l63);
                        gm_tl874t2atnlh3 t322 = gm_l60;
                        gm_b16_8 t323 = gm_l59;
                        gm_tnpifv20irnph t324;
                        GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
                        gm_f_format_40d716h((void *)&gm_l99, (void *)NULL, cx, NULL, t324);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t325 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                        if (!t325) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 8, gm_srcuktvltncsesc, 298, 32);
                        gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l99) + 8));
                        gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                        gm_f_add_label_50fj7vv(t321, GM_CONV(gm_cseahvg8v7p0vs, t322), t327);
                    }
                    {
                        void * t328 = (void *)((uint8_t *)&F.l63);
                        gm_b16_8 t329 = gm_l59;
                        gm_b16_8 t330 = gm_l62;
                        gm_t6j6smj7qoctu t331;
                        GM_ST(gm_b16_8, ((uint8_t *)&t331 + 0), t329);
                        GM_ST(gm_b16_8, ((uint8_t *)&t331 + 16), t330);
                        gm_f_format_mqu5cbu((void *)&gm_l100, (void *)NULL, cx, NULL, &t331);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                        if (!t332) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 8, gm_srcuktvltncsesc, 299, 24);
                        gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                        gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                        gm_f_add_note_46v502s(t328, t334);
                    }
                    {
                        gm_t4gut2c9fj1kt t335 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l63));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t335);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t337 = (uint8_t)(t336 == ((uint8_t)10ULL));
                    if (!t337) {
                        goto gm_lb27;
                    }
                    gm_tl874t2atnlh3 t338 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l64 = t338;
                    gm_b16_8 t339 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l65 = t339;
                    gm_tl874t2atnlh3 t340 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l66 = t340;
                    gm_tl874t2atnlh3 t341 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 44));
                    gm_l67 = t341;
                    void * t342 = gm_l0;
                    gm_tl874t2atnlh3 t343 = gm_l66;
                    gm_b16_8 t344 = gm_f_text_of_lbn39it(t342, GM_CONV(gm_cseahvg8v7p0vs, t343));
                    gm_l68 = t344;
                    {
                        gm_tl874t2atnlh3 t345 = gm_l64;
                        gm_b16_8 t346;
                        GM_ST(void *, (uint8_t *)&t346, (void *)(gm_su3sef888krmb));
                        GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(21));
                        gm_b16_8 t347 = gm_l65;
                        gm_b16_8 t348 = gm_l68;
                        gm_t6j6smj7qoctu t349;
                        GM_ST(gm_b16_8, ((uint8_t *)&t349 + 0), t347);
                        GM_ST(gm_b16_8, ((uint8_t *)&t349 + 16), t348);
                        gm_f_format_iqr642o((void *)&gm_l101, (void *)NULL, cx, NULL, &t349);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t350 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                        if (!t350) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcuktvltncsesc, 304, 59);
                        gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                        gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&t351));
                        gm_t4gut2c9fj1kt t353;
                        gm_f_lint_diag_4vra4qu((void *)&t353, (void *)NULL, cx, NULL, t345, t346, t352);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l69 = t353;
                    }
                    {
                        void * t354 = (void *)((uint8_t *)&F.l69);
                        gm_tl874t2atnlh3 t355 = gm_l67;
                        gm_b16_8 t356 = gm_l65;
                        gm_tnpifv20irnph t357;
                        GM_ST(gm_b16_8, ((uint8_t *)&t357 + 0), t356);
                        gm_f_format_duvvd80((void *)&gm_l102, (void *)NULL, cx, NULL, t357);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                        if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 8, gm_srcuktvltncsesc, 305, 33);
                        gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l102) + 8));
                        gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                        gm_f_add_label_50fj7vv(t354, GM_CONV(gm_cseahvg8v7p0vs, t355), t360);
                    }
                    void * t361 = (void *)((uint8_t *)&F.l69);
                    gm_b16_8 t362;
                    GM_ST(void *, (uint8_t *)&t362, (void *)(gm_svo4dku3ftf1l));
                    GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(176));
                    gm_f_add_note_46v502s(t361, t362);
                    {
                        gm_t4gut2c9fj1kt t363 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l69));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t363);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb27:;
                {
                    uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t365 = (uint8_t)(t364 == ((uint8_t)3ULL));
                    if (!t365) {
                        goto gm_lb28;
                    }
                    gm_tl874t2atnlh3 t366 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l70 = t366;
                    gm_tl874t2atnlh3 t367 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 12));
                    gm_l71 = t367;
                    uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 24));
                    gm_l72 = t368;
                    void * t369 = gm_l0;
                    gm_tl874t2atnlh3 t370 = gm_l70;
                    gm_b16_8 t371 = gm_f_text_of_lbn39it(t369, GM_CONV(gm_cseahvg8v7p0vs, t370));
                    gm_l73 = t371;
                    gm_b16_8 t372 = gm_l73;
                    gm_b16_8 t373;
                    GM_ST(void *, (uint8_t *)&t373, (void *)(gm_sehko5bb96s6p));
                    GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(4));
                    uint8_t t374 = gm_f_contains_gkki4kq(t372, t373);
                    if (t374) {
                        gm_l103 = ((uint8_t)1);
                    } else {
                        gm_b16_8 t375 = gm_l73;
                        gm_b16_8 t376;
                        GM_ST(void *, (uint8_t *)&t376, (void *)(gm_sogehh8t1m7tn));
                        GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(5));
                        uint8_t t377 = gm_f_contains_gkki4kq(t375, t376);
                        gm_l103 = t377;
                    }
                    uint8_t t378 = gm_l103;
                    uint8_t t379 = (uint8_t)!t378;
                    if (t379) {
                        memset(&(*(gm_tkc9ngh8g1s0j *)gm_out), 0, sizeof (*(gm_tkc9ngh8g1s0j *)gm_out));
                        goto gm_fdone;
                    }
                    void * t380 = gm_l0;
                    gm_tl874t2atnlh3 t381 = gm_l71;
                    gm_b16_8 t382 = gm_f_text_of_lbn39it(t380, GM_CONV(gm_cseahvg8v7p0vs, t381));
                    gm_l74 = t382;
                    gm_b16_8 t383 = gm_l74;
                    gm_l75 = t383;
                    uint8_t t384 = gm_l72;
                    uint8_t t385 = (uint8_t)!t384;
                    if (t385) {
                        gm_b16_8 t386 = gm_l74;
                        gm_b16_8 t387;
                        GM_ST(void *, (uint8_t *)&t387, (void *)(gm_southrlg4k72m));
                        GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(5));
                        uint8_t t388 = gm_f_contains_gkki4kq(t386, t387);
                        if (t388) {
                            gm_l105 = ((uint8_t)1);
                        } else {
                            gm_b16_8 t389 = gm_l74;
                            gm_b16_8 t390;
                            GM_ST(void *, (uint8_t *)&t390, (void *)(gm_sbf1f7fu8bvpt));
                            GM_ST(uintptr_t, (uint8_t *)&t390 + 8, (uintptr_t)(4));
                            uint8_t t391 = gm_f_contains_gkki4kq(t389, t390);
                            gm_l105 = t391;
                        }
                        uint8_t t392 = gm_l105;
                        if (t392) {
                            gm_l104 = ((uint8_t)1);
                        } else {
                            gm_b16_8 t393 = gm_l74;
                            gm_b16_8 t394;
                            GM_ST(void *, (uint8_t *)&t394, (void *)(gm_spp2t9pj35hcq));
                            GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(6));
                            uint8_t t395 = gm_f_contains_gkki4kq(t393, t394);
                            gm_l104 = t395;
                        }
                        uint8_t t396 = gm_l104;
                        if (t396) {
                            gm_b16_8 t397 = gm_l74;
                            gm_tnpifv20irnph t398;
                            GM_ST(gm_b16_8, ((uint8_t *)&t398 + 0), t397);
                            gm_f_format_a2pi3bt((void *)&gm_l106, (void *)NULL, cx, NULL, t398);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t399 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                            if (!t399) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 8, gm_srcuktvltncsesc, 316, 35);
                            gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l106) + 8));
                            gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                            gm_l75 = t401;
                        } else {
                            gm_b16_8 t402 = gm_l74;
                            gm_tnpifv20irnph t403;
                            GM_ST(gm_b16_8, ((uint8_t *)&t403 + 0), t402);
                            gm_f_format_udg5t2e((void *)&gm_l107, (void *)NULL, cx, NULL, t403);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t404 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                            if (!t404) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 8, gm_srcuktvltncsesc, 318, 35);
                            gm_b16_8 t405 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
                            gm_b16_8 t406 = GM_LD(gm_b16_8, ((uint8_t *)&t405));
                            gm_l75 = t406;
                        }
                    }
                    {
                        gm_tl874t2atnlh3 t407 = gm_l70;
                        gm_b16_8 t408;
                        GM_ST(void *, (uint8_t *)&t408, (void *)(gm_srvn31r258t3t));
                        GM_ST(uintptr_t, (uint8_t *)&t408 + 8, (uintptr_t)(15));
                        gm_b16_8 t409;
                        GM_ST(void *, (uint8_t *)&t409, (void *)(gm_s1gd0gkbe5tu9));
                        GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(54));
                        gm_t4gut2c9fj1kt t410;
                        gm_f_lint_diag_4vra4qu((void *)&t410, (void *)NULL, cx, NULL, t407, t408, t409);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l76 = t410;
                    }
                    {
                        void * t411 = (void *)((uint8_t *)&F.l76);
                        gm_b16_8 t412 = gm_l75;
                        gm_tnpifv20irnph t413;
                        GM_ST(gm_b16_8, ((uint8_t *)&t413 + 0), t412);
                        gm_f_format_ooibcq2((void *)&gm_l108, (void *)NULL, cx, NULL, t413);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t414 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                        if (!t414) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 8, gm_srcuktvltncsesc, 322, 23);
                        gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l108) + 8));
                        gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)&t415));
                        gm_tl874t2atnlh3 t417 = gm_l70;
                        gm_b16_8 t418 = gm_l75;
                        gm_b0_1 t419 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t411, t416, t417, t418);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_t4gut2c9fj1kt t420 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l76));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t420);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t422 = (uint8_t)(t421 == ((uint8_t)4ULL));
                    if (!t422) {
                        goto gm_lb29;
                    }
                    gm_tl874t2atnlh3 t423 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l77 = t423;
                    uint8_t t424 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 12));
                    gm_l78 = t424;
                    {
                        gm_tl874t2atnlh3 t425 = gm_l77;
                        gm_b16_8 t426;
                        GM_ST(void *, (uint8_t *)&t426, (void *)(gm_siooblm685vmo));
                        GM_ST(uintptr_t, (uint8_t *)&t426 + 8, (uintptr_t)(15));
                        uint8_t t427 = gm_l78;
                        gm_t9qkh0rsooc8d t428;
                        GM_ST(uint8_t, ((uint8_t *)&t428 + 0), t427);
                        gm_f_format_5mja5ge((void *)&gm_l109, (void *)NULL, cx, NULL, t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t429 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                        if (!t429) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l109) + 8, gm_srcuktvltncsesc, 325, 84);
                        gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l109) + 8));
                        gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&t430));
                        gm_t4gut2c9fj1kt t432;
                        gm_f_lint_diag_4vra4qu((void *)&t432, (void *)NULL, cx, NULL, t425, t426, t431);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t432);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb29:;
                {
                    uint8_t t433 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t434 = (uint8_t)(t433 == ((uint8_t)5ULL));
                    if (!t434) {
                        goto gm_lb30;
                    }
                    gm_tl874t2atnlh3 t435 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l79 = t435;
                    {
                        gm_tl874t2atnlh3 t436 = gm_l79;
                        gm_b16_8 t437;
                        GM_ST(void *, (uint8_t *)&t437, (void *)(gm_sgu334n5g1kog));
                        GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(15));
                        gm_b16_8 t438;
                        GM_ST(void *, (uint8_t *)&t438, (void *)(gm_sijvlope91krv));
                        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(30));
                        gm_t4gut2c9fj1kt t439;
                        gm_f_lint_diag_4vra4qu((void *)&t439, (void *)NULL, cx, NULL, t436, t437, t438);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out)), t439);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 211, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_in_scope_tm13267 F;
    void * gm_l0; /* sc */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* owner */
    gm_b16_8 gm_l5; /* known */
    GM_STACK_CHECK(&F, 623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint8_t gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_tftb1odc739t5 gm_l12;
    uint8_t gm_l13;
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    gm_b16_8 t6 = gm_l2;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    uint8_t t9 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t6, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!t9;
    if (t10) {
        gm_l6 = ((uint8_t)1);
    } else {
        gm_b16_8 t11 = gm_l2;
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
        uint8_t t14 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t14) {
            void * t15 = gm_l0;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 32));
            uint8_t t17 = (uint8_t)!t16;
            gm_l7 = t17;
        } else {
            gm_l7 = ((uint8_t)0);
        }
        uint8_t t18 = gm_l7;
        gm_l6 = t18;
    }
    uint8_t t19 = gm_l6;
    if (t19) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t20 = gm_l2;
    gm_f_parent_lk0iero((void *)&gm_l8, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t21) {
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l3 = t22;
    } else {
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
        gm_l3 = t23;
    }
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(0));
    gm_l4 = t24;
    {
        {
            void * t25 = gm_l0;
            void * t26 = (void *)(((uint8_t *)t25) + 40);
            gm_b16_8 t27 = gm_l3;
            gm_f_get_e3joqcc((void *)&gm_l9, t26, t27);
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t28) {
                goto gm_lb2;
            }
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l5 = t29;
            gm_b16_8 t30 = gm_l5;
            gm_l4 = t30;
            goto gm_lb1;
        }
gm_lb2:;
        gm_b16_8 t31 = gm_l3;
        gm_f_find_root_vg2av48((void *)&gm_l11, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
        if (t32) {
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l10 = t33;
        } else {
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(0));
            gm_l10 = t34;
        }
        gm_b16_8 t35 = gm_l10;
        gm_l4 = t35;
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 40);
        gm_b16_8 t38 = gm_l3;
        gm_b16_8 t39 = gm_l4;
        gm_tftb1odc739t5 t40 = gm_f_put_7shfu9u(t37, t38, t39);
        gm_l12 = t40;
        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcuktvltncsesc, 133, 9);
    }
gm_lb1:;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
    if (t43) {
        gm_l13 = ((uint8_t)1);
    } else {
        gm_b16_8 t44 = gm_l4;
        gm_b16_8 t45 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t46 = gm_l0;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)t46));
        uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t47)));
        gm_l13 = t48;
    }
    uint8_t t49 = gm_l13;
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_name_qqj3mmj F;
    gm_t9qkh0rsooc8d gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_slb6cj7ndgvdm));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_soofuqhnovv5o));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb4;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s920j0georape));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 29, 45);
    }
gm_lb1:;
    gm_b16_8 t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_level_4iat2k8 F;
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
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_slb6cj7ndgvdm));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
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
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_soofuqhnovv5o));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
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
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s920j0georape));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
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
        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 22, 47);
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
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4) {
    struct gm_fr_run_n6la0ov F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* parsed */
    uint8_t gm_l2; /* program */
    void * gm_l3; /* sc */
    void * gm_l4; /* cfg */
    void * gm_l5; /* sm */
    void * gm_l6; /* diags */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    void * gm_l24; /* p */
    uint64_t gm_l25; /* before */
    void * gm_l26; /* facts */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_t2lqu8jsj511d gm_l31; /* out */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* code */
    uint8_t gm_l41; /* allowed */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_b16_8 gm_l47; /* r */
    GM_STACK_CHECK(&F, 2927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l48;
    gm_t7qqq0mh1r8ke gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tntfcv47jbeh7 gm_l51;
    uint8_t gm_l52;
    uint64_t gm_l53;
    gm_tf5srh4mp4b3i gm_l54;
    gm_tkc9ngh8g1s0j gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    uint8_t gm_l59;
    gm_t9qkh0rsooc8d gm_l60;
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        void * t52 = GM_LD(void *, ((uint8_t *)t51));
        gm_l5 = t52;
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, ((uint8_t *)t53));
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 8));
        gm_l6 = t55;
        void * t56 = gm_l6;
        uint8_t t57 = gm_f_has_errors_if7ai0m(t56);
        if (t57) {
            memset(&(*(gm_tc70a9dbbmgkg *)gm_out), 0, sizeof (*(gm_tc70a9dbbmgkg *)gm_out));
            goto gm_fdone;
        }
        gm_trqfvmjtt4sij t58;
        gm_f_list_s83jn2q((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t58;
        void * t59 = gm_l5;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)t59));
        gm_l8 = t60;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t61 = gm_l9;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t63 = (t61 < t62);
                if (!t63) {
                    goto gm_lb1;
                }
                {
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t66 = gm_l9;
                    {
                        {
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t64) + (uintptr_t)t66 * 80), 80);
                            void * t67 = (void *)((uint8_t *)&F.l7);
                            void * t68 = gm_l3;
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                            uint8_t t70 = gm_f_in_scope_tm13267((void *)NULL, cx, NULL, t68, t69);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t71 = gm_f_push_vd1290p(t67, t70);
                            gm_l48 = t71;
                            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcuktvltncsesc, 156, 31);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 156, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t73 = gm_l9;
                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                uint64_t t76 = r74;
                gm_l9 = t76;
            }
        }
gm_lb1:;
        void * t77 = gm_l0;
        uint8_t t78 = gm_l2;
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
        gm_f_analyze_np1m50h((void *)&F.l11, (void *)NULL, cx, NULL, t77, t78, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t81;
        gm_f_list_prbqo9j((void *)&t81, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t81;
        void * t82 = gm_l5;
        gm_t1og5k0mlcjk3 t83;
        gm_f_walk_tech5q5((void *)&t83, (void *)NULL, cx, NULL, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t83;
        gm_t4c7ipjj9s3fp t84;
        gm_f_hash_map_cuh4fs9((void *)&t84, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t84;
        gm_t4c7ipjj9s3fp t85;
        gm_f_hash_map_s7cd6p5((void *)&t85, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t85;
        gm_b16_8 t86 = gm_l1;
        gm_l16 = t86;
    }
    {
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t87 = gm_l17;
                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t89 = (t87 < t88);
                if (!t89) {
                    goto gm_lb6;
                }
                {
                    {
                        uint64_t t90 = gm_l17;
                        void * t91 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t93 = gm_l17;
                        gm_tqtt2php6e8lk t94 = GM_LD(gm_tqtt2php6e8lk, (((uint8_t *)t91) + (uintptr_t)t93 * 56));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l49 + 0), t90);
                        GM_ST(gm_tqtt2php6e8lk, ((uint8_t *)&gm_l49 + 8), t94);
                    }
                    {
                        {
                            uint64_t t95 = GM_LD(uint64_t, ((uint8_t *)&gm_l49));
                            gm_l18 = t95;
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)&gm_l49) + 8), 56);
                            void * t96 = (void *)((uint8_t *)&F.l15);
                            uint32_t t97 = GM_LD(uint32_t, ((uint8_t *)&F.l19));
                            uint64_t t98 = gm_l18;
                            gm_tftb1odc739t5 t99 = gm_f_put_sokl06f(t96, t97, t98);
                            gm_l50 = t99;
                            uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcuktvltncsesc, 162, 37);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 162, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t101 = gm_l17;
                uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                uint64_t t104 = r102;
                gm_l17 = t104;
            }
        }
gm_lb6:;
        void * t105 = gm_l5;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)t105));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
        gm_l20 = t107;
        gm_l21 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t108 = gm_l21;
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint8_t t110 = (t108 < t109);
                if (!t110) {
                    goto gm_lb11;
                }
                {
                    {
                        uint64_t t111 = gm_l21;
                        void * t112 = GM_LD(void *, ((uint8_t *)&gm_l20));
                        uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint64_t t114 = gm_l21;
                        gm_tcsu09l1pp5hf t115 = GM_LD(gm_tcsu09l1pp5hf, (((uint8_t *)t112) + (uintptr_t)t114 * 80));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l51 + 0), t111);
                        GM_ST(gm_tcsu09l1pp5hf, ((uint8_t *)&gm_l51 + 8), t115);
                    }
                    {
                        {
                            uint64_t t116 = GM_LD(uint64_t, ((uint8_t *)&gm_l51));
                            gm_l22 = t116;
                            memmove(((uint8_t *)&F.l23), (((uint8_t *)&gm_l51) + 8), 80);
                            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                            gm_b16_8 t118;
                            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s1f440st4mnkp));
                            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(1));
                            uint8_t t119 = gm_f_starts_with_6vvt01e(t117, t118);
                            if (t119) {
                                gm_l52 = ((uint8_t)1);
                            } else {
                                void * t120 = GM_LD(void *, ((uint8_t *)&F.l7));
                                uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                uint64_t t122 = gm_l22;
                                if ((uintptr_t)t122 >= (uintptr_t)(t121)) gm_trap_index((uintptr_t)t122, t121, gm_srcuktvltncsesc, 164, 43);
                                uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)t120) + (uintptr_t)t122 * 1));
                                uint8_t t124 = (uint8_t)!t123;
                                gm_l52 = t124;
                            }
                            uint8_t t125 = gm_l52;
                            if (t125) {
                                goto gm_lb13;
                            }
                            void * t126 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            void * t128 = (void *)((uint8_t *)&F.l15);
                            uint64_t t129 = gm_l22;
                            uint32_t t130 = (uint32_t)t129;
                            gm_tf5srh4mp4b3i t131 = gm_f_get_1ji4too(t128, t130);
                            gm_l54 = t131;
                            uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l54))[8] != 0);
                            if (t132) {
                                uint64_t t133 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                gm_l53 = t133;
                            } else {
                                goto gm_lb13;
                            }
                            uint64_t t134 = gm_l53;
                            if ((uintptr_t)t134 >= (uintptr_t)(t127)) gm_trap_index((uintptr_t)t134, t127, gm_srcuktvltncsesc, 167, 15);
                            void * t135 = (void *)(((uint8_t *)t126) + (uintptr_t)t134 * 56);
                            gm_l24 = t135;
                            uint64_t t136 = GM_LD(uint64_t, (((((uint8_t *)&F.l13) + 8) + 96) + 8));
                            gm_l25 = t136;
                            void * t137 = (void *)((uint8_t *)&F.l13);
                            void * t138 = gm_l24;
                            void * t139 = (void *)(((uint8_t *)t138) + 24);
                            void * t140 = gm_l24;
                            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)t140) + 8));
                            gm_b0_1 t142 = gm_f_walk_file_do4uq8b((void *)NULL, cx, NULL, t137, t139, t141);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t143 = gm_l24;
                            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t143) + 8));
                            void * t145 = GM_LD(void *, ((((uint8_t *)&F.l13) + 8) + 96));
                            uint64_t t146 = GM_LD(uint64_t, (((((uint8_t *)&F.l13) + 8) + 96) + 8));
                            uint64_t t147 = gm_l25;
                            uint64_t t148 = (uintptr_t)t147;
                            uint64_t t149 = (uintptr_t)t146;
                            if (t148 > t149 || t149 > (uintptr_t)t146) gm_trap_slice(t148, t149, t146, gm_srcuktvltncsesc, 170, 28);
                            uint64_t t150 = ((uintptr_t)t146) - (uintptr_t)t147;
                            gm_b16_8 t151;
                            GM_ST(void *, (uint8_t *)&t151, (void *)(((uint8_t *)t145 + (uintptr_t)t147 * 12)));
                            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(t150));
                            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                            void * t153 = (void *)((uint8_t *)&F.l14);
                            gm_b0_1 t154 = gm_f_names_in_1jbgo4k((void *)NULL, cx, NULL, t144, t152, t153);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 163, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t155 = gm_l21;
                uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                uint64_t t158 = r156;
                gm_l21 = t158;
            }
        }
gm_lb11:;
    }
    {
        void * t159 = (void *)(((uint8_t *)&F.l13) + 8);
        gm_l26 = t159;
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_l27 = t160;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t161 = gm_l28;
                uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t163 = (t161 < t162);
                if (!t163) {
                    goto gm_lb16;
                }
                {
                    void * t164 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t166 = gm_l28;
                    {
                        {
                            memmove(((uint8_t *)&F.l29), (((uint8_t *)t164) + (uintptr_t)t166 * 72), 72);
                            {
                                {
                                    {
                                        void * t167 = gm_l5;
                                        void * t168 = (void *)((uint8_t *)&F.l29);
                                        void * t169 = gm_l26;
                                        void * t170 = (void *)(((uint8_t *)t169) + 40);
                                        void * t171 = (void *)((uint8_t *)&F.l14);
                                        gm_f_finding_diag_2vp4thv((void *)&gm_l55, (void *)NULL, cx, NULL, t167, t168, t170, t171);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t172 = (uint8_t)((((uint8_t *)&gm_l55))[184] != 0);
                                    if (!t172) {
                                        goto gm_lb22;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l55), 184);
                                    }
                                    {
                                        void * t173 = (void *)((uint8_t *)&F.l12);
                                        gm_tftb1odc739t5 t174 = gm_f_push_l0rgbjf(t173, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l30)));
                                        gm_l56 = t174;
                                    }
                                    uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                    if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcuktvltncsesc, 174, 67);
                                    goto gm_lb21;
                                }
gm_lb22:;
                            }
gm_lb21:;
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 173, 5);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t176 = gm_l28;
                uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                uint64_t t179 = r177;
                gm_l28 = t179;
            }
        }
gm_lb16:;
        void * t180 = gm_l26;
        uint64_t t181 = GM_LD(uint64_t, ((((uint8_t *)t180) + 176) + 8));
        gm_t2lqu8jsj511d t182;
        GM_ST(uint64_t, ((uint8_t *)&t182 + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t182 + 8), t181);
        gm_l31 = t182;
        void * t183 = gm_l26;
        gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)t183) + 176));
        gm_l32 = t184;
        gm_l33 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t185 = gm_l33;
                uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                uint8_t t187 = (t185 < t186);
                if (!t187) {
                    goto gm_lb23;
                }
                {
                    void * t188 = GM_LD(void *, ((uint8_t *)&gm_l32));
                    uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint64_t t190 = gm_l33;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l34), (((uint8_t *)t188) + (uintptr_t)t190 * 184), 184);
                            }
                            {
                                void * t191 = gm_l6;
                                gm_f_push_o4ckdr1(t191, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l34)));
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 177, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t192 = gm_l33;
                uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                uint64_t t195 = r193;
                gm_l33 = t195;
            }
        }
gm_lb23:;
    }
    {
        gm_trqfvmjtt4sij t196;
        gm_f_list_prbqo9j((void *)&t196, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l35 = t196;
        void * t197 = (void *)((uint8_t *)&F.l35);
        void * t198 = gm_l26;
        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)t198));
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        gm_tftb1odc739t5 t201 = gm_f_extend_s4sbegm(t197, t200);
        gm_l57 = t201;
        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcuktvltncsesc, 180, 5);
        void * t203 = (void *)((uint8_t *)&F.l35);
        gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
        gm_tftb1odc739t5 t206 = gm_f_extend_s4sbegm(t203, t205);
        gm_l58 = t206;
        uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
        if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcuktvltncsesc, 181, 5);
        gm_l37 = ((uint64_t)0ULL);
        uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
        gm_l36 = t208;
        {
            for (;;) {
                uint64_t t209 = gm_l37;
                uint64_t t210 = gm_l36;
                uint8_t t211 = (t209 < t210);
                if (!t211) {
                    goto gm_lb28;
                }
                {
                    {
                        {
                            uint64_t t212 = gm_l37;
                            gm_l38 = t212;
                            {
                                void * t213 = GM_LD(void *, ((uint8_t *)&F.l35));
                                uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
                                uint64_t t215 = gm_l38;
                                if ((uintptr_t)t215 >= (uintptr_t)(t214)) gm_trap_index((uintptr_t)t215, t214, gm_srcuktvltncsesc, 183, 14);
                                memmove(((uint8_t *)&F.l39), (((uint8_t *)t213) + (uintptr_t)t215 * 184), 184);
                            }
                            uint8_t t216 = (uint8_t)(((((uint8_t *)&F.l39) + 160))[16] != 0);
                            if (t216) {
                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 160));
                                gm_l40 = t217;
                            } else {
                                gm_b16_8 t218;
                                GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(0));
                                gm_l40 = t218;
                            }
                            gm_l41 = ((uint8_t)0);
                            void * t219 = gm_l26;
                            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)t219) + 136));
                            gm_l42 = t220;
                            gm_l43 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t221 = gm_l43;
                                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                    uint8_t t223 = (t221 < t222);
                                    if (!t223) {
                                        goto gm_lb33;
                                    }
                                    {
                                        void * t224 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                        uint64_t t226 = gm_l43;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l44), (((uint8_t *)t224) + (uintptr_t)t226 * 56), 56);
                                                {
                                                    {
                                                        uint32_t t227 = GM_LD(uint32_t, ((uint8_t *)&F.l44));
                                                        uint32_t t228 = GM_LD(uint32_t, (((uint8_t *)&F.l39) + 24));
                                                        uint8_t t229 = ((t227 == t228));
                                                        if (!t229) {
                                                            goto gm_lb39;
                                                        }
                                                        uint32_t t230 = GM_LD(uint32_t, (((uint8_t *)&F.l44) + 4));
                                                        uint32_t t231 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 4));
                                                        uint8_t t232 = (t230 <= t231);
                                                        if (!t232) {
                                                            goto gm_lb39;
                                                        }
                                                        uint32_t t233 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 8));
                                                        uint32_t t234 = GM_LD(uint32_t, (((uint8_t *)&F.l44) + 8));
                                                        uint8_t t235 = (t233 <= t234);
                                                        if (!t235) {
                                                            goto gm_lb39;
                                                        }
                                                        gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 16));
                                                        gm_l45 = t236;
                                                        gm_l46 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t237 = gm_l46;
                                                                uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                                uint8_t t239 = (t237 < t238);
                                                                if (!t239) {
                                                                    goto gm_lb40;
                                                                }
                                                                {
                                                                    void * t240 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                                                    uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                                    uint64_t t242 = gm_l46;
                                                                    {
                                                                        {
                                                                            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)t240) + (uintptr_t)t242 * 16));
                                                                            gm_l47 = t243;
                                                                            gm_b16_8 t244 = gm_l47;
                                                                            gm_b16_8 t245 = gm_l40;
                                                                            uint8_t t246 = (gm_eq0iqiubfm7e(((uint8_t *)&t244), ((uint8_t *)&t245)));
                                                                            if (t246) {
                                                                                gm_l59 = ((uint8_t)1);
                                                                            } else {
                                                                                gm_b16_8 t247 = gm_l47;
                                                                                gm_b16_8 t248;
                                                                                GM_ST(void *, (uint8_t *)&t248, (void *)(gm_s0nf1rg04f3m5));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(3));
                                                                                uint8_t t249 = (gm_eq0iqiubfm7e(((uint8_t *)&t247), ((uint8_t *)&t248)));
                                                                                gm_l59 = t249;
                                                                            }
                                                                            uint8_t t250 = gm_l59;
                                                                            if (t250) {
                                                                                gm_l41 = ((uint8_t)1);
                                                                            }
                                                                            goto gm_lb43;
                                                                        }
gm_lb44:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 188, 17);
                                                                    }
gm_lb43:;
                                                                }
gm_lb42:;
                                                                uint64_t t251 = gm_l46;
                                                                uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                                                uint64_t t254 = r252;
                                                                gm_l46 = t254;
                                                            }
                                                        }
gm_lb40:;
                                                        goto gm_lb38;
                                                    }
gm_lb39:;
                                                }
gm_lb38:;
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 186, 9);
                                        }
gm_lb36:;
                                    }
gm_lb35:;
                                    uint64_t t255 = gm_l43;
                                    uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                    uint64_t t258 = r256;
                                    gm_l43 = t258;
                                }
                            }
gm_lb33:;
                            uint8_t t259 = gm_l41;
                            if (t259) {
                                goto gm_lb30;
                            }
                            void * t260 = gm_l4;
                            gm_b16_8 t261 = gm_l40;
                            gm_t9qkh0rsooc8d t262 = gm_f_level_m5neg79(t260, t261);
                            gm_l60 = t262;
                            {
                                {
                                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)0ULL));
                                    if (!t264) {
                                        goto gm_lb46;
                                    }
                                    goto gm_lb30;
                                }
gm_lb46:;
                                {
                                    uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                                    uint8_t t266 = (uint8_t)(t265 == ((uint8_t)1ULL));
                                    if (!t266) {
                                        goto gm_lb47;
                                    }
                                    uint64_t t267 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                    uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                                    if (o269) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 196, 24);
                                    uint64_t t270 = r268;
                                    GM_ST(uint64_t, ((uint8_t *)&gm_l31), t270);
                                    goto gm_lb45;
                                }
gm_lb47:;
                                {
                                    uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                                    uint8_t t272 = (uint8_t)(t271 == ((uint8_t)2ULL));
                                    if (!t272) {
                                        goto gm_lb48;
                                    }
                                    gm_t9qkh0rsooc8d t273;
                                    GM_ST(uint8_t, ((uint8_t *)&t273 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l39), t273);
                                    uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                    uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                                    if (o276) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 199, 17);
                                    uint64_t t277 = r275;
                                    GM_ST(uint64_t, (((uint8_t *)&gm_l31) + 8), t277);
                                    goto gm_lb45;
                                }
gm_lb48:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 194, 9);
                            }
gm_lb45:;
                            {
                                void * t278 = gm_l6;
                                gm_f_push_o4ckdr1(t278, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l39)));
                            }
                            goto gm_lb31;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 182, 5);
                    }
gm_lb31:;
                }
gm_lb30:;
                uint64_t t279 = gm_l37;
                uint64_t r280; bool o281 = __builtin_add_overflow(t279, ((uint64_t)1ULL), &r280);
                uint64_t t282 = r280;
                gm_l37 = t282;
            }
        }
gm_lb28:;
    }
    {
        gm_t2lqu8jsj511d t283 = gm_l31;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 20, 0);
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out)), t283);
        ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out))[16] = 1;
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
extern __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_set_5cf9rlg F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    gm_t9qkh0rsooc8d gm_l2; /* level */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_b16_8 t7 = gm_l1;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s0nf1rg04f3m5));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
    uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
    if (t9) {
        gm_l4 = ((uint64_t)0ULL);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)gm_ciuj8hs0q3l));
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
                            void * t15 = gm_l0;
                            void * t16 = GM_LD(void *, ((uint8_t *)t15));
                            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
                            uint64_t t18 = gm_l5;
                            if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcuktvltncsesc, 63, 34);
                            gm_t9qkh0rsooc8d t19 = gm_l2;
                            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t16) + (uintptr_t)t18 * 1), t19);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 63, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t20 = gm_l4;
                uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                uint64_t t23 = r21;
                gm_l4 = t23;
            }
        }
gm_lb1:;
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t24 = gm_l1;
    gm_tf5srh4mp4b3i t25 = gm_f_rule_scmud9h(t24);
    gm_l7 = t25;
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
    if (t26) {
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l6 = t27;
    } else {
        {
            gm_b16_8 t28 = gm_l1;
            gm_tnpifv20irnph t29;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
            gm_f_format_tovpseb((void *)&gm_l8, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcuktvltncsesc, 66, 33);
        {
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t32);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    void * t33 = gm_l0;
    void * t34 = GM_LD(void *, ((uint8_t *)t33));
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
    uint64_t t36 = gm_l6;
    if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srcuktvltncsesc, 67, 5);
    gm_t9qkh0rsooc8d t37 = gm_l2;
    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t34) + (uintptr_t)t36 * 1), t37);
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_whole_lines_s23hlj6 F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* src */
    uint64_t gm_l3; /* lo */
    uint64_t gm_l4; /* hi */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* end */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    gm_b16_8 gm_l9; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_b16_8 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_b16_8 gm_l13;
    uint8_t gm_l14;
    void * t10 = gm_l0;
    uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t12 = gm_f_file_o15fqku(t10, t11);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
    gm_l2 = t13;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t15 = (uint64_t)t14;
    gm_l3 = t15;
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t17 = (uint64_t)t16;
    gm_l4 = t17;
    uint64_t t18 = gm_l3;
    gm_l5 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l5;
            uint8_t t20 = (t19 > ((uint64_t)0ULL));
            if (!t20) {
                goto gm_lb1;
            }
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t23 = gm_l5;
            uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 29);
            uint64_t t26 = r24;
            if ((uintptr_t)t26 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t26, t22, gm_srcuktvltncsesc, 337, 25);
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t26 * 1));
            uint8_t t28 = (!(t27 == ((uint8_t)10ULL)));
            if (!t28) {
                goto gm_lb1;
            }
            {
                uint64_t t29 = gm_l5;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 54);
                uint64_t t32 = r30;
                gm_l5 = t32;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t33 = gm_l4;
    gm_l6 = t33;
    {
        for (;;) {
            uint64_t t34 = gm_l6;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t36 = (t34 < t35);
            if (!t36) {
                goto gm_lb4;
            }
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t39 = gm_l6;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)t39 * 1));
            uint8_t t41 = (!(t40 == ((uint8_t)10ULL)));
            if (!t41) {
                goto gm_lb4;
            }
            {
                uint64_t t42 = gm_l6;
                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 339, 52);
                uint64_t t45 = r43;
                gm_l6 = t45;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    uint64_t t46 = gm_l6;
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t48 = (t46 < t47);
    if (t48) {
        uint64_t t49 = gm_l6;
        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
        if (o51) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 340, 24);
        uint64_t t52 = r50;
        gm_l6 = t52;
    }
    void * t53 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t55 = gm_l5;
    uint64_t t56 = gm_l3;
    uint64_t t57 = (uintptr_t)t55;
    uint64_t t58 = (uintptr_t)t56;
    if (t57 > t58 || t58 > (uintptr_t)t54) gm_trap_slice(t57, t58, t54, gm_srcuktvltncsesc, 341, 15);
    uint64_t t59 = ((uintptr_t)t56) - (uintptr_t)t55;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t53 + (uintptr_t)t55 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(t59));
    gm_l7 = t60;
    void * t61 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t63 = gm_l4;
    uint64_t t64 = gm_l6;
    uint64_t t65 = (uintptr_t)t63;
    uint64_t t66 = (uintptr_t)t64;
    if (t65 > t66 || t66 > (uintptr_t)t62) gm_trap_slice(t65, t66, t62, gm_srcuktvltncsesc, 342, 14);
    uint64_t t67 = ((uintptr_t)t64) - (uintptr_t)t63;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t61 + (uintptr_t)t63 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67));
    gm_l8 = t68;
    gm_b16_8 t69 = gm_l7;
    gm_b16_8 t70 = gm_f_trim_fdrfe6i(t69);
    gm_l10 = t70;
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
    uint8_t t72 = (t71 > ((uint64_t)0ULL));
    if (t72) {
        gm_toceffb697p0u t73;
        memset(&t73, 0, sizeof t73);
        F.ret = t73;
        goto gm_fret;
    }
    gm_b16_8 t74 = gm_l8;
    gm_b16_8 t75 = gm_f_trim_start_179crak(t74);
    gm_l9 = t75;
    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
    if (t77) {
        gm_l12 = ((uint8_t)1);
    } else {
        gm_b16_8 t78 = gm_l9;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_si4j8sh2qika1));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(2));
        uint8_t t80 = gm_f_starts_with_6vvt01e(t78, t79);
        gm_l12 = t80;
    }
    uint8_t t81 = gm_l12;
    if (t81) {
        gm_l11 = ((uint8_t)1);
    } else {
        gm_b16_8 t82 = gm_l8;
        gm_b16_8 t83 = gm_f_trim_fdrfe6i(t82);
        gm_l13 = t83;
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
        uint8_t t85 = ((t84 == ((uint64_t)0ULL)));
        gm_l11 = t85;
    }
    uint8_t t86 = gm_l11;
    uint8_t t87 = (uint8_t)!t86;
    if (t87) {
        gm_toceffb697p0u t88;
        memset(&t88, 0, sizeof t88);
        F.ret = t88;
        goto gm_fret;
    }
    {
        {
            uint64_t t89 = gm_l5;
            uint8_t t90 = ((t89 == ((uint64_t)0ULL)));
            if (t90) {
                gm_l14 = ((uint8_t)1);
            } else {
                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t93 = gm_l5;
                uint64_t t94 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t95 = (uintptr_t)t93;
                if (t94 > t95 || t95 > (uintptr_t)t92) gm_trap_slice(t94, t95, t92, gm_srcuktvltncsesc, 348, 23);
                uint64_t t96 = ((uintptr_t)t93) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t91 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(t96));
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s8dbdqj1sqibj));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(2));
                uint8_t t99 = gm_f_ends_with_biriu5l(t97, t98);
                gm_l14 = t99;
            }
            uint8_t t100 = gm_l14;
            if (!t100) {
                goto gm_lb8;
            }
            void * t101 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t103 = gm_l6;
            uint64_t t104 = (uintptr_t)t103;
            uint64_t t105 = (uintptr_t)t102;
            if (t104 > t105 || t105 > (uintptr_t)t102) gm_trap_slice(t104, t105, t102, gm_srcuktvltncsesc, 348, 59);
            uint64_t t106 = ((uintptr_t)t102) - (uintptr_t)t103;
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t101 + (uintptr_t)t103 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106));
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(1));
            uint8_t t109 = gm_f_starts_with_6vvt01e(t107, t108);
            if (!t109) {
                goto gm_lb8;
            }
            uint64_t t110 = gm_l6;
            uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
            if (o112) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 348, 90);
            uint64_t t113 = r111;
            gm_l6 = t113;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    uint32_t t114 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t115 = gm_l5;
    uint64_t t116 = gm_l6;
    gm_tl874t2atnlh3 t117 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t114, t115, t116));
    gm_toceffb697p0u t118;
    GM_ST(uint8_t, (uint8_t *)&t118 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t118 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t118), t117);
    ((uint8_t *)&t118)[12] = 1;
    F.ret = t118;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
